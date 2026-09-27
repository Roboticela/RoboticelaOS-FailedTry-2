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
    case RoboCssPropertyComputationFlexDirection:
        return "flex-direction";
    case RoboCssPropertyComputationFlexWrap:
        return "flex-wrap";
    case RoboCssPropertyComputationJustifyContent:
        return "justify-content";
    case RoboCssPropertyComputationAlignItems:
        return "align-items";
    case RoboCssPropertyComputationAlignSelf:
        return "align-self";
    case RoboCssPropertyComputationAlignContent:
        return "align-content";
    case RoboCssPropertyComputationFlexFactor:
        return "flex-factor";
    case RoboCssPropertyComputationFlexBasis:
        return "flex-basis";
    case RoboCssPropertyComputationOrder:
        return "order";
    case RoboCssPropertyComputationGridAutoFlow:
        return "grid-auto-flow";
    case RoboCssPropertyComputationGridGap:
        return "grid-gap";
    case RoboCssPropertyComputationTableLayout:
        return "table-layout";
    case RoboCssPropertyComputationBorderCollapse:
        return "border-collapse";
    case RoboCssPropertyComputationCaptionSide:
        return "caption-side";
    case RoboCssPropertyComputationEmptyCells:
        return "empty-cells";
    case RoboCssPropertyComputationVisibility:
        return "visibility";
    case RoboCssPropertyComputationColumnCount:
        return "column-count";
    case RoboCssPropertyComputationColumnWidth:
        return "column-width";
    case RoboCssPropertyComputationColumnSpan:
        return "column-span";
    case RoboCssPropertyComputationColumnFill:
        return "column-fill";
    case RoboCssPropertyComputationDirection:
        return "direction";
    case RoboCssPropertyComputationWritingMode:
        return "writing-mode";
    case RoboCssPropertyComputationTextOrientation:
        return "text-orientation";
    case RoboCssPropertyComputationTransformStyle:
        return "transform-style";
    case RoboCssPropertyComputationBackfaceVisibility:
        return "backface-visibility";
    case RoboCssPropertyComputationTransitionBehavior:
        return "transition-behavior";
    case RoboCssPropertyComputationAnimationDirection:
        return "animation-direction";
    case RoboCssPropertyComputationAnimationFillMode:
        return "animation-fill-mode";
    case RoboCssPropertyComputationAnimationPlayState:
        return "animation-play-state";
    case RoboCssPropertyComputationAnimationComposition:
        return "animation-composition";
    case RoboCssPropertyComputationAnimationIterationCount:
        return "animation-iteration-count";
    case RoboCssPropertyComputationContainerType:
        return "container-type";
    case RoboCssPropertyComputationFontStyle:
        return "font-style";
    case RoboCssPropertyComputationFontKerning:
        return "font-kerning";
    case RoboCssPropertyComputationFontOpticalSizing:
        return "font-optical-sizing";
    case RoboCssPropertyComputationFontSynthesis:
        return "font-synthesis";
    case RoboCssPropertyComputationFontVariantCaps:
        return "font-variant-caps";
    case RoboCssPropertyComputationFontVariantPosition:
        return "font-variant-position";
    case RoboCssPropertyComputationTextAlign:
        return "text-align";
    case RoboCssPropertyComputationTextAlignLast:
        return "text-align-last";
    case RoboCssPropertyComputationTextJustify:
        return "text-justify";
    case RoboCssPropertyComputationTextTransform:
        return "text-transform";
    case RoboCssPropertyComputationWhiteSpace:
        return "white-space";
    case RoboCssPropertyComputationWordBreak:
        return "word-break";
    case RoboCssPropertyComputationOverflowWrap:
        return "overflow-wrap";
    case RoboCssPropertyComputationLineBreak:
        return "line-break";
    case RoboCssPropertyComputationHyphens:
        return "hyphens";
    case RoboCssPropertyComputationTextDecorationStyle:
        return "text-decoration-style";
    case RoboCssPropertyComputationImageRendering:
        return "image-rendering";
    case RoboCssPropertyComputationObjectFit:
        return "object-fit";
    case RoboCssPropertyComputationBackgroundBox:
        return "background-box";
    case RoboCssPropertyComputationBlendMode:
        return "blend-mode";
    case RoboCssPropertyComputationBorderImageRepeat:
        return "border-image-repeat";
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
    /* `direction`'s shape is upgraded in place for 18.13: the KEYWORD shape accepted any
     * identifier, so `direction: banana` computed to a keyword carrying the letters. It
     * joins the writing vocabulary's closed set (`ltr`, `rtl`); its inheritance and its
     * `ltr` initial value are unchanged, and `ltr` is the zero of the enumerator. */
    { "direction",     RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationDirection,  "ltr",        0 },
    { "font-family",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontFamily,"serif",      0 },
    /*
     * --- the font family (18.14) ---
     *
     * Eighteen rows, all inherited, and the family they belong to is the first in
     * this table whose values describe a *request* rather than a result: `font-weight:
     * 700` asks for a face, and which face answers is the font database's business
     * (RoboFont, and 12.17's selection seam for a shell document). Nothing here reads
     * a font file, so nothing here can be wrong about which face exists.
     *
     * Two kinds of row sit in this block and the difference is the *grammar*, not the
     * importance. Six properties have a genuinely closed set and take a typed shape:
     * `font-kerning`, `font-optical-sizing`, the four `font-synthesis-*` longhands (one
     * shape, because the four take the same two values), `font-variant-caps` and
     * `font-variant-position` — plus `font-style`, whose shape is *upgraded in place*
     * from the bare KEYWORD it had since 9.7. The rest have a grammar wider than any
     * closed set — `font-stretch` admits a percentage, `font-variant-numeric` is a
     * space-separated set of independent axes, `font-feature-settings` is a list of
     * tag/value pairs — so they are registered PRESERVED and the closed sets *inside*
     * them live in RoboCSSFont.h, exactly as 18.7 typed the names inside a `transform`
     * list while carrying the list.
     *
     * `font-variant-ligatures` is the row that is deliberately carried without a
     * vocabulary in this module: its five tags and their policy are RoboText's (12.12,
     * `RoboTextLigature.h`), and a second copy here is how the shape a CSS author
     * writes and the policy a shaper reads would come to disagree.
     */
    { "font-feature-settings", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "normal",     0 },
    { "font-kerning",  RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontKerning,"auto",       0 },
    { "font-language-override", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "normal",     0 },
    { "font-optical-sizing", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontOpticalSizing, "auto", 0 },
    { "font-palette",  RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "normal",     0 },
    { "font-size",     RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationLength,     "medium",     0 },
    { "font-stretch",  RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "normal",     0 },
    { "font-style",    RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontStyle,  "normal",     0 },
    { "font-synthesis", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "auto auto auto auto",
      RoboCssPropertyFlagShorthand },
    { "font-synthesis-position", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontSynthesis, "auto",     0 },
    { "font-synthesis-small-caps", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontSynthesis, "auto",     0 },
    { "font-synthesis-style", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontSynthesis, "auto",     0 },
    { "font-synthesis-weight", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontSynthesis, "auto",     0 },
    { "font-variant",  RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationKeyword,    "normal",     0 },
    { "font-variant-alternates", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "normal",     0 },
    { "font-variant-caps", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontVariantCaps, "normal", 0 },
    { "font-variant-east-asian", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "normal",     0 },
    { "font-variant-ligatures", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "normal",     0 },
    { "font-variant-numeric", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "normal",     0 },
    { "font-variant-position", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontVariantPosition, "normal", 0 },
    { "font-variation-settings", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "normal",     0 },
    { "font-weight",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontWeight,"normal",     0 },
    /*
     * --- the text family (18.15) ---
     *
     * Fourteen inherited rows join this block, and three rows that were already
     * here are **upgraded in place**: `text-align`, `text-transform` and
     * `white-space` move off the bare KEYWORD shape and onto closed vocabularies,
     * with their inheritance and initial values unchanged. The KEYWORD shape accepted
     * any identifier, so `white-space: banana` used to compute to a keyword carrying
     * the letters; the closed set is what refuses it.
     *
     * The family's rows split the same way 18.7's and 18.14's do. Nine take a typed
     * shape — `text-align`, `text-align-last`, `text-justify`, `text-transform`,
     * `white-space`, `word-break`, `overflow-wrap`, `line-break`, `hyphens` and
     * `text-decoration-style` — and seven are carried declarations whose grammar is a
     * *set* or a wider family: a decoration line list, an overflow marker that may be
     * a string, a thickness, an underline position, an emphasis mark, an emphasis
     * position and the two shorthands above them. `text-indent` keeps the shared
     * length shape it has had since 9.11, and its `hanging`/`each-line` keywords are
     * named as not read rather than silently dropped.
     *
     * `text-decoration-*` and `text-overflow` are the **non-inherited** half and live
     * in the block below; `text-shadow`, `text-emphasis-*`, `text-underline-*` and the
     * line-breaking controls are all inherited, which is why the inherited count
     * moves by fourteen and the other by six.
     */
    { "hyphens",       RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationHyphens,    "manual",     0 },
    /*
     * --- the image family (18.16) ---
     *
     * Three rows join the inherited block and two the non-inherited one, and the
     * split is the family's home stated as a count: how a chosen image is *rendered*
     * and how a file's own orientation is *read* are settings of the element that
     * holds the image, so `image-rendering`, `image-orientation` and
     * `image-resolution` propagate to a child that displays the same image; how a
     * replaced element's content is *fitted to its box* is the box's own business, so
     * `object-fit` and `object-position` do not.
     *
     * Two rows take a closed shape. `image-rendering`'s five words are not two
     * algorithms (see the shape above), and `object-fit`'s five are not one scale
     * factor. `image-orientation` is carried because its grammar is
     * `from-image | none | <angle>` with an optional `flip`, and its keyword pair is
     * typed inside the declaration in RoboCSSImage.h; `image-resolution` is carried
     * because its grammar is `[ from-image || <resolution> ] && snap`; and
     * `object-position` is carried because it is the position *pair* the background
     * module owns rather than a second copy of that grammar here.
     *
     * `image-orientation: from-image` is the family's `from-font`: a fact about the
     * *file* rather than a policy of the user agent, which is why it is the initial
     * and why discarding it is the answer a build that cannot read the value must not
     * give.
     */
    { "image-orientation", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,  "from-image", 0 },
    { "image-rendering", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationImageRendering, "auto",   0 },
    { "image-resolution", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,  "1dppx",      0 },
    { "letter-spacing",RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationLength,     "normal",     0 },
    { "line-break",    RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationLineBreak,  "auto",       0 },
    { "line-height",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationLineHeight,"normal",     0 },
    { "overflow-wrap", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationOverflowWrap, "normal",   0 },
    /* `tab-size` is carried: its grammar is `normal | <number> | <length>` with the
     * number written as a bare token, which no closed set describes and this build
     * has no tab stop to resolve. */
    { "tab-size",      RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "8",          0 },
    { "text-align",    RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationTextAlign,  "start",      0 },
    { "text-align-last", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationTextAlignLast, "auto",     0 },
    /* The emphasis family is registered and **not typed**: its values are a shape
     * list of eight kinds with a filled/open pair over them, and nothing in this
     * build draws an emphasis mark. Carrying the declarations keeps the cascade and
     * the computed style correct; typing the marks is the phase that draws one, and
     * this is the boundary stated rather than implied.
     *
     * `text-emphasis-color` is a colour like every other colour, so it takes the 9.9
     * shape rather than a string, and its `currentColor` initial is the registry's
     * fifth *reference* initial. */
    { "text-emphasis", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "none currentcolor",
      RoboCssPropertyFlagShorthand },
    { "text-emphasis-color", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationColor,      "currentcolor", 0 },
    { "text-emphasis-position", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "over right", 0 },
    { "text-emphasis-style", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "none",       0 },
    { "text-indent",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationLength,     "0",          0 },
    { "text-justify",  RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationTextJustify,"auto",       0 },
    /* `text-shadow` is inherited, which surprises people once and then makes sense:
     * a shadow is a property of the *text*, so a `span` inside a shadowed paragraph
     * casts the same shadow. Its grammar is a comma-separated list, which is why the
     * row is carried. */
    { "text-shadow",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "none",       0 },
    { "text-transform",RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationTextTransform, "none",    0 },
    { "text-underline-offset", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "auto",       0 },
    { "text-underline-position", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,   "auto",       0 },
    { "word-break",    RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationWordBreak,  "normal",     0 },
    /* `visibility` is a registered property of this table since 9.7 and its shape is
     * upgraded here: the KEYWORD shape accepted any identifier, so `visibility: banana`
     * computed to a keyword carrying the letters, which is exactly the silent failure
     * every closed vocabulary in this module exists to move forward in time. It joins
     * the table vocabulary's closed set (`visible`, `hidden`, `collapse`) while keeping
     * the inheritance and initial value it always had. */
    { "visibility",    RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationVisibility, "visible",    0 },
    /* `white-space` is upgraded in place like `text-align` above: six spellings that
     * used to compute to a keyword carrying any identifier now compute to the closed
     * set whose four classifications are the property's actual content. */
    { "white-space",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationWhiteSpace, "normal",     0 },
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
     * --- 18.17 completes the background family ---
     *
     * Three rows join the six longhands 9.14 added. `background-clip` and
     * `background-origin` are both the *box* vocabulary and differ only in their
     * initial value — `border-box` for the clip, `padding-box` for the origin — which
     * is the one-vocabulary-two-initials fact the display and positioning rows record;
     * and `background-blend-mode` is a vocabulary of its own, the sixteen modes it
     * shares with `mix-blend-mode`.
     *
     * All three are non-inherited for the reason the six are: a background is painted
     * per element. And all three are per-layer in CSS while this build holds one layer,
     * so the list form is refused rather than half-read — the same boundary 9.14 drew
     * for the image and the position.
     */
    { "background-blend-mode", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBlendMode,  "normal",      0 },
    { "background-clip",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBackgroundBox, "border-box", 0 },
    { "background-origin",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBackgroundBox, "padding-box", 0 },
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
     * --- 18.18 completes the border family ---
     *
     * Three families of rows the 9.15 phase named as not built: the `border-image`
     * decorations, the `outline` family, and `border-radius` with its four corners.
     *
     * Only one of the sixteen takes a shape of its own. `border-image-repeat` is a
     * genuine closed set — `stretch | repeat | round | space` — and a second copy of
     * the background repeat idea for the border image's middle; the rest reuse a shape
     * or are carried. `border-image-source` is `none | <image>`, so it reuses 9.14's
     * `BACKGROUND_IMAGE` shape unchanged; the slice, width, outset and radius grammars
     * are all wider than any closed set (a fill keyword, a slash-separated pair, a
     * length-percentage pair), so they are carried; and `outline-style` reuses 9.15's
     * `BORDER_STYLE`, `outline-width` the shared `LINE_WIDTH` and `outline-color` the
     * 9.9 colour. That is the same typed/carried split every phase since 9.14 uses.
     *
     * All sixteen are non-inherited, because a border, an outline and an image are all
     * painted per element. `outline-color` is the fifth row here (after the four border
     * colour rows) whose initial is the *reference* `currentcolor`.
     */
    { "border-image",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "none",
      RoboCssPropertyFlagShorthand },
    { "border-image-outset", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",          0 },
    { "border-image-repeat", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBorderImageRepeat, "stretch", 0 },
    { "border-image-slice", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "100%",       0 },
    { "border-image-source", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBackgroundImage, "none",   0 },
    { "border-image-width", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "1",          0 },
    { "border-radius",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",
      RoboCssPropertyFlagShorthand },
    /* The four corners in CSS's own order, top-left round to bottom-left. */
    { "border-top-left-radius", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",          0 },
    { "border-top-right-radius", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",          0 },
    { "border-bottom-right-radius", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",          0 },
    { "border-bottom-left-radius", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",          0 },
    { "outline",          RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "medium none currentcolor",
      RoboCssPropertyFlagShorthand },
    { "outline-color",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColor,      "currentcolor", 0 },
    { "outline-offset",   RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",          0 },
    { "outline-style",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBorderStyle, "none",       0 },
    { "outline-width",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLineWidth,  "medium",     0 },

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
    /*
     * `font` is the one row of 18.14's family that is **not inherited**, and the
     * omission is not an oversight: a shorthand's own computed value is the tokens
     * an author wrote (PRESERVE), the distribution over its longhands is expansion's
     * job, and a shorthand that *were* marked inherited would propagate a whole
     * concatenation where CSS propagates each longhand by its own rule. So this row
     * carries `normal normal normal normal medium/normal serif` — the concatenation
     * of the seven longhands' initial values, the same construction `border` records
     * as `medium none currentcolor`.
     */
    { "font",          RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,
      "normal normal normal normal medium/normal serif",
      RoboCssPropertyFlagShorthand },
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
     * --- the non-inherited half of the image family (18.16) ---
     *
     * How a replaced element's content is fitted to its box belongs to the box, not
     * to the content: a child of a `cover`-fitting element that displayed the same
     * image is not itself `cover`. So `object-fit` takes the closed shape and
     * `object-position` is carried as the position pair the background module owns.
     */
    { "object-fit",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationObjectFit, "fill",        0 },
    { "object-position", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,  "50% 50%",     0 },
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
    /*
     * --- the non-inherited half of the text family (18.15) ---
     *
     * A decoration is painted per element, so the four `text-decoration-*` rows and
     * the shorthand are non-inherited and live here rather than with the fourteen
     * above — a child of an underlined `span` is not underlined, and a build that
     * inherited the line would draw every descendant of a link as a link.
     *
     * Only one of the five takes a typed shape: `text-decoration-style` is its own
     * closed set, while the line is a *set* (`underline overline` is legal), the
     * colour is the shared 9.9 colour shape with the same `currentColor` initial every
     * other colour property has, and the thickness is `auto | from-font | <length>`.
     * `text-overflow` is here for the same reason: truncation is about the box that
     * clips, not about the text inside it.
     *
     * `text-decoration`'s initial is the concatenation of its four longhands'
     * initials, which is what CSS says and what makes the shorthand's own distribution
     * agree with the longhands' values. */
    { "text-decoration",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,          "none currentcolor solid auto",
      RoboCssPropertyFlagShorthand },
    { "text-decoration-color", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColor,             "currentcolor", 0 },
    { "text-decoration-line",  RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,          "none",       0 },
    { "text-decoration-style", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationTextDecorationStyle, "solid",    0 },
    { "text-decoration-thickness", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,          "auto",       0 },
    { "text-overflow",         RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,          "clip",       0 },
    { "top",           RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "auto",      0 },
    { "width",         RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLength,     "auto",       0 },
    { "z-index",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationZIndex,    "auto",       0 },

    /*
     * --- the flexbox family (18.3) ---
     *
     * Twelve rows, gathered here rather than scattered through the table by
     * alphabetisation, for the reason the border family is gathered: the names fall
     * across the whole table (`align-*` before `background`, `order` beside `overflow`,
     * `flex-*` around `display`), and a reader meeting any one of them should be able
     * to see the other eleven.
     *
     * Three facts about the block are decisions rather than transcriptions.
     *
     * The **alignment properties share one vocabulary and take four shapes**. That is
     * CSS Box Alignment's own structure, and it is why this table has four computation
     * rows for one value set: which values `justify-content`, `align-items`, `align-self`
     * and `align-content` may each hold is not the same question, so a single shape would
     * make `align-items: space-between` computable. The difference lives in the vocabulary
     * module's rows and the four shapes read it.
     *
     * The **initial values are the properties' own and two of them are not zero.**
     * `align-self` starts at `auto` — "take the container's `align-items`" — and
     * `flex-shrink` starts at `1`, so a cleared record would hold the wrong answer for
     * both; the registry is where that is written down, exactly as it is for `opacity`.
     * The initial values are `normal` about the family in the common case, which is the
     * value every one of the four alignment properties accepts.
     *
     * The **two shorthands are PRESERVE**, and deliberately not a vocabulary. `flex-flow`
     * is `<flex-direction> || <flex-wrap>` and `flex` is
     * `none | <flex-grow> <flex-shrink>? || <flex-basis>`; each stands for two halves of
     * *different kinds*, so neither can take one closed vocabulary the way `overflow`
     * took its own. They carry what the author wrote, and the boundary that nothing in
     * this build expands a shorthand is stated in `Flexbox.md`.
     */
    { "align-content",  RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationAlignContent, "normal",   0 },
    { "align-items",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationAlignItems,   "normal",   0 },
    { "align-self",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationAlignSelf,    "auto",     0 },
    { "flex",           RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "0 1 auto",
      RoboCssPropertyFlagShorthand },
    { "flex-basis",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationFlexBasis,    "auto",     0 },
    { "flex-direction", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationFlexDirection, "row",     0 },
    { "flex-flow",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "row nowrap",
      RoboCssPropertyFlagShorthand },
    { "flex-grow",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationFlexFactor,   "0",        0 },
    { "flex-shrink",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationFlexFactor,   "1",        0 },
    { "flex-wrap",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationFlexWrap,     "nowrap",   0 },
    { "justify-content", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationJustifyContent, "normal", 0 },
    { "order",          RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationOrder,        "0",        0 },

    /*
     * --- the grid family (18.4) ---
     *
     * Twenty rows, gathered here rather than scattered by alphabetisation, for the
     * reason the flexbox block above is: the names fall across the table and a reader
     * meeting one should be able to see the rest.
     *
     * Two of the rows are *typed* and eighteen are carried declarations, and the split
     * is the honest one rather than a shortcut. `grid-auto-flow` is a closed set of four
     * spellings, so it is read and classified like `flex-wrap`; `row-gap` and
     * `column-gap` (and their legacy `grid-*-gap` aliases) are a length family, so they
     * join the one length grammar every other length-shaped property uses. Everything
     * else here takes a *track list* — a size, a `minmax()`, a repeat count, a line
     * name, a `span` — and that grammar is a phase of its own; this build carries those
     * declarations PRESERVED rather than pretending to read them, exactly as `flex-flow`
     * and `flex` are carried. A PRESERVED row is not a placeholder: it is the author's
     * declaration, structurally intact, with the registry supplying its inheritance and
     * its initial value so the cascade and the computed style are still correct.
     *
     * None of the twenty inherits: a grid container's flow, its track template, its line
     * placements and its gutters are the box's own, and a child that inherited a template
     * would place itself into its parent's grid.
     */
    { "column-gap",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationGridGap,      "normal",   0 },
    { "gap",            RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "normal",
      RoboCssPropertyFlagShorthand },
    { "grid",           RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "none",
      RoboCssPropertyFlagShorthand },
    { "grid-area",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "auto",
      RoboCssPropertyFlagShorthand },
    { "grid-auto-columns", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "auto",     0 },
    { "grid-auto-flow", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationGridAutoFlow, "row",      0 },
    { "grid-auto-rows", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "auto",     0 },
    { "grid-column",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "auto",
      RoboCssPropertyFlagShorthand },
    { "grid-column-end", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "auto",     0 },
    { "grid-column-gap", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationGridGap,      "normal",   0 },
    { "grid-column-start", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "auto",     0 },
    { "grid-row",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "auto",
      RoboCssPropertyFlagShorthand },
    { "grid-row-end",   RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "auto",     0 },
    { "grid-row-gap",   RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationGridGap,      "normal",   0 },
    { "grid-row-start", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "auto",     0 },
    { "grid-template",  RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "none",
      RoboCssPropertyFlagShorthand },
    { "grid-template-areas", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "none",     0 },
    { "grid-template-columns", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "none",     0 },
    { "grid-template-rows", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "none",     0 },
    { "row-gap",        RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationGridGap,      "normal",   0 },

    /*
     * --- the table family (18.5) ---
     *
     * Five new rows (`visibility` was already here and has its shape upgraded above),
     * and the family is the first whose members are *mostly inherited*:
     * a table's cells take the collapse, the spacing, the caption side and the empty-cell
     * rule from the table box rather than deciding them for themselves, while
     * `table-layout` is the box's own. That split is the whole reason these rows are
     * gathered here rather than alphabetised: getting one of them non-inherited would make
     * every cell of a table that declared it inside the wrong box lay itself out by the
     * rule an ancestor wrote.
     *
     * Five of the rows take a closed vocabulary of their own. `border-spacing` does not:
     * its grammar is `<length>{1,2}`, the shared length grammar with a pair above it, and
     * this build carries the declaration PRESERVED until the phase that reads it (19.6).
     * A PRESERVED row is still correct -- it carries the author's declaration, its
     * inheritance and its initial value -- it simply is not interpreted here.
     */
    { "border-collapse", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationBorderCollapse, "separate", 0 },
    { "border-spacing",  RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationPreserve,     "0",        0 },
    { "caption-side",    RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationCaptionSide,  "top",      0 },
    { "empty-cells",     RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationEmptyCells,   "show",     0 },
    { "table-layout",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationTableLayout,  "auto",     0 },

    /*
     * --- the multi-column family (18.6) ---
     *
     * Nine rows, none inherited: a fragmentation container's column count, width,
     * span, fill rule and rule decoration are the box's own, and a child that inherited
     * a column count would split *itself* into that many columns. `column-gap` was
     * already registered by 18.4 (it is the same gutter `row-gap` is) and is not repeated.
     *
     * Four of the rows take shapes the table already has. `column-rule-width`, 
     * `column-rule-style` and `column-rule-color` are the border family's line width, border
     * style and colour shapes rather than a second copy of any of the three; `column-width`
     * is the shared length grammar with the keyword `auto`; `column-count` is the one numeric
     * shape of its own (a positive integer or `auto`); and the two shorthands `column-rule`
     * and `columns` are carried declarations like every other shorthand here.
     */
    { "column-count",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColumnCount,  "auto",     0 },
    { "column-fill",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColumnFill,   "balance",  0 },
    { "column-rule",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "medium none currentColor",
      RoboCssPropertyFlagShorthand },
    { "column-rule-color", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColor,        "currentcolor", 0 },
    { "column-rule-style", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBorderStyle,  "none",     0 },
    { "column-rule-width", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLineWidth,    "medium",   0 },
    { "column-span",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColumnSpan,   "none",     0 },
    { "column-width",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColumnWidth,  "auto",     0 },
    { "columns",         RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,     "auto auto",
      RoboCssPropertyFlagShorthand },

    /*
     * --- the writing-mode family (18.13) ---
     *
     * Two new rows beside the `direction` row upgraded above. All three are
     * **inherited**: the writing mode and the orientation decide which physical edge
     * is inline-start and how a character stands, and a child that did not inherit them
     * would be laid out on axes its parent was not on. `writing-mode` starts at
     * `horizontal-tb` and `text-orientation` at `mixed`, which are each the zero of
     * their enumerator, so a cleared record is already right. `text-orientation` is
     * carried and inert in `horizontal-tb`, exactly as `background-attachment: fixed`
     * is a value with no scrollport; the build's text engine does not yet shape the
     * `sideways-*` values, and they are read here as the values they are.
     */
    { "writing-mode",     RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationWritingMode,  "horizontal-tb", 0 },
    { "text-orientation", RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationTextOrientation, "mixed",    0 },

    /*
     * --- the transform family (18.7) ---
     *
     * Nine rows, none inherited: a transform is the box's own (a child that
     * inherited its parent's transform would move with it rather than with the
     * layout). Two take closed vocabularies — `transform-style` (`flat` |
     * `preserve-3d`) and `backface-visibility` (`visible` | `hidden`) — and the
     * rest carry their declarations, because a transform *list*, an origin, a
     * perspective and the three individual transform properties are grammars the
     * phase names rather than parses; what this phase supplies is the closed set of
     * function names a list is built from (`CssTransformFunction`). The initials are
     * `none` for the four lists and the two origins' own `50% 50%`, and `flat` /
     * `visible` are each the zero of their enumerator.
     */
    { "transform",           RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "none",      0 },
    { "transform-origin",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "50% 50%",   0 },
    { "transform-style",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationTransformStyle, "flat",      0 },
    { "perspective",         RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "none",      0 },
    { "perspective-origin",  RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "50% 50%",   0 },
    { "backface-visibility", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBackfaceVisibility, "visible", 0 },
    { "translate",           RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "none",      0 },
    { "rotate",              RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "none",      0 },
    { "scale",               RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "none",      0 },

    /*
     * --- the transition family (18.8) ---
     *
     * Six rows, none inherited: a transition is the box's own. Five of them are
     * *grammars* -- a property-name list, a time list, a timing-function list and
     * the shorthand that concatenates all four -- so they are carried declarations.
     * One is a closed set, `transition-behavior`, which is the phase's typed shape.
     * The timing-function *keywords* (`ease`, `linear`, `step-start`, ...) are the
     * set this phase supplies for a consumer to read; the functional forms
     * (`cubic-bezier()`, `steps()`) are part of the property's grammar and are
     * carried rather than represented.
     */
    { "transition-behavior", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationTransitionBehavior, "normal", 0 },
    { "transition-delay",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "0s",        0 },
    { "transition-duration", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "0s",        0 },
    { "transition-property", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "all",       0 },
    { "transition-timing-function", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "ease",      0 },
    { "transition",          RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "all 0s ease 0s",
      RoboCssPropertyFlagShorthand },

    /*
     * --- the animation family (18.9) ---
     *
     * Ten rows, none inherited. Four take closed vocabularies -- `animation-direction`,
     * `animation-fill-mode`, `animation-play-state` and `animation-composition` -- and
     * `animation-iteration-count` is the one numeric shape of its own (`infinite`, or a
     * non-negative number, where a fraction is legal). The rest are grammars: a
     * keyframes name, a time list, a timing-function list and the shorthand that
     * concatenates all of them. `animation-timing-function` reuses the transition
     * family's timing-function keywords rather than growing a second copy. A
     * `@keyframes` at-rule is the source an animation name refers to, not a property,
     * and is not registered here.
     */
    { "animation",           RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "none 0s ease 0s 1 normal none running",
      RoboCssPropertyFlagShorthand },
    { "animation-composition", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationAnimationComposition, "replace", 0 },
    { "animation-delay",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "0s",        0 },
    { "animation-direction", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationAnimationDirection, "normal", 0 },
    { "animation-duration",  RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "0s",        0 },
    { "animation-fill-mode", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationAnimationFillMode, "none", 0 },
    { "animation-iteration-count", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationAnimationIterationCount, "1", 0 },
    { "animation-name",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "none",      0 },
    { "animation-play-state", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationAnimationPlayState, "running", 0 },
    { "animation-timing-function", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "ease",      0 },

    /*
     * --- the container-query family (18.11) ---
     *
     * Three rows, none inherited. `container-type` is the phase's one typed shape
     * (`normal` | `size` | `inline-size`); the name list and the shorthand are carried,
     * exactly as the grid track templates are. `@container` itself is a prelude, not a
     * property.
     */
    { "container-type",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationContainerType, "normal",    0 },
    { "container-name",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "none",      0 },
    { "container",           RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve, "none / normal",
      RoboCssPropertyFlagShorthand },

    /*
     * --- the logical-property family (18.12) ---
     *
     * Eighteen rows, none inherited. A logical longhand is carried as the *same shape*
     * its physical counterpart is -- `margin-block-start` is an `EdgeOffset` like
     * `margin-top`, `padding-inline-end` a `Length` like `padding-right`,
     * `inline-size` a `Length` `auto` like `width` -- because turning a role into the
     * physical edge it names is the resolver's job (`RoboCSSLogical.h`), not a second
     * value model. The four shorthands are carried declarations like every other
     * shorthand here. Not built and named: the logical border longhands, `min-`/`max-`
     * logical sizes, `border-start-start-radius` and the logical shorthands of the
     * insets beyond the two axes.
     */
    { "margin-block-start",  RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "0",         0 },
    { "margin-block-end",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "0",         0 },
    { "margin-inline-start", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "0",         0 },
    { "margin-inline-end",   RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "0",         0 },
    { "padding-block-start", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLength,     "0",         0 },
    { "padding-block-end",   RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLength,     "0",         0 },
    { "padding-inline-start", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLength,     "0",         0 },
    { "padding-inline-end",  RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLength,     "0",         0 },
    { "inset-block-start",   RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "auto",      0 },
    { "inset-block-end",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "auto",      0 },
    { "inset-inline-start",  RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "auto",      0 },
    { "inset-inline-end",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "auto",      0 },
    { "block-size",          RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLength,     "auto",      0 },
    { "inline-size",         RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLength,     "auto",      0 },
    { "margin-block",        RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",
      RoboCssPropertyFlagShorthand },
    { "margin-inline",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",
      RoboCssPropertyFlagShorthand },
    { "padding-block",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",
      RoboCssPropertyFlagShorthand },
    { "padding-inline",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",
      RoboCssPropertyFlagShorthand }
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
