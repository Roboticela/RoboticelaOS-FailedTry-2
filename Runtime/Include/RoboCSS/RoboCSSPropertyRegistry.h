/*
 * RoboCSS property registry (Phase 9.7).
 *
 * RoboCSSProperty.h answers "which property is this?" with an opaque identity
 * that a table issues. That is identity, and identity is not enough for
 * inheritance, which has to answer two different questions about a property it
 * has already identified:
 *
 *   does it inherit by default?        color yes, width no
 *   what is its initial value?         color -> black, width -> auto
 *
 * This header is those two answers, in one place. It is deliberately *not* a
 * second property table: it issues no ids, resolves no names to identities and
 * holds no per-caller state a declaration could be resolved against. It is a
 * description of CSS's own standard properties, and the inheritance phase reads
 * it so that no inheritance decision is a hard-coded name comparison buried in
 * a resolution path.
 *
 * Two objects, not one
 *
 *   CssPropertyTable      identity, per caller, filled by the caller
 *   CssPropertyRegistry   classification + initial values, CSS's own
 *
 * They meet through CssPropertyRegistryApplyToTable, which registers
 * every property the registry knows into a caller's table with the right
 * RoboCssPropertyFlagInherited bit. That is the supported way to build a
 * table, and it is why the two cannot drift: the registry is where the bit
 * comes from.
 *
 * A caller that fills its own table by hand is still heard: a property the
 * registry has never heard of inherits when the table says its flag is set. The
 * precedence is stated once, in RoboCSSInherit.h, because that is where the
 * decision is actually made.
 *
 * Ids are the registry's business to *not* have
 *
 * A definition is found by name, and every accessor hands out a borrowed
 * pointer to a definition that never moves. Entries are individually allocated
 * and the registry holds pointers to them, so a later registration cannot
 * relocate an earlier definition — a borrowed definition stays valid until the
 * registry is destroyed, which is a rule a caller can actually rely on rather
 * than one that happens to hold until the next call.
 *
 * Initial values are parsed, once
 *
 * A definition's initial value is stored as text *and* as the component-value
 * tree that text parses to, built at registration time through the same
 * grammar a declaration's value goes through. Inheritance therefore never
 * reparses an initial value per element, and an initial value cannot be a
 * different dialect of CSS from a declared one.
 *
 * The registry stores text and a value tree; it does not interpret either.
 * `margin: 0` is the four zeroes the shorthand stands for only once something
 * expands shorthands, and nothing here does — which is why RoboCssPropertyFlagShorthand
 * is carried on the definition and nothing acts on it yet.
 */

#ifndef ROBO_CSS_PROPERTY_REGISTRY_H
#define ROBO_CSS_PROPERTY_REGISTRY_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Whether a property inherits by default.
 *
 * UNKNOWN is the honest answer for a property no registry has classified — it
 * is not the same as NON_INHERITED, and a caller that conflates them is making
 * an assumption rather than reading a fact. The inheritance phase resolves
 * UNKNOWN to "does not inherit" and *says so*, because CSS's own fallback for a
 * property it does not know is that the declaration is dropped, and dropping a
 * value is worse than not propagating it.
 */
typedef enum CssPropertyInheritance {
    /** No classification: not registered here. */
    RoboCssPropertyInheritanceUnknown = 0,
    /** Inherits by default (`color`, `font-size`, every custom property). */
    RoboCssPropertyInheritanceInherited = 1,
    /** Does not inherit (`width`, `display`, `margin`). */
    RoboCssPropertyInheritanceNonInherited = 2
} CssPropertyInheritance;

/** Number of defined classifications (not counting UNKNOWN). */
#define RoboCssPropertyInheritanceCount 3u

/** Non-zero when @p inheritance is a defined enumerator. */
int CssPropertyInheritanceIsKnown(CssPropertyInheritance inheritance);

/** Human-readable classification name ("inherited", "non-inherited"); never NULL. */
const char *CssPropertyInheritanceName(
    CssPropertyInheritance inheritance);

/**
 * How a property's declared value becomes a *computed* value (9.8).
 *
 * Inheritance needed two facts about a property. Computation needs a third, and
 * it is a different kind of fact: not "what is this property" but "what shape
 * is its value". `color` holds a colour, `width` a length-or-keyword, `margin`
 * whatever its shorthand says — and a computed value model that does not know
 * the shape cannot normalise anything.
 *
 * So the shape is recorded here, beside the other two, and the computed-value
 * normaliser dispatches on it. That is what keeps the dispatch out of a single
 * enormous switch in the computation code: the *property* says what it is, and
 * the computation reads the answer instead of re-deriving it from the name.
 *
 * PRESERVE is the honest default and the answer for every property nothing has
 * classified — including an unknown property, a shorthand (whose longhands are
 * not expanded yet) and a custom property. It means "carry the tokens", which
 * is a computed representation in the same sense a length is: it is the value,
 * structurally intact, with no interpretation claimed.
 */
typedef enum CssPropertyComputation {
    /** Carry the token sequence structurally; interpret nothing. */
    RoboCssPropertyComputationPreserve = 0,
    /** A colour: named, `transparent`, hex or `rgb()`/`rgba()`. */
    RoboCssPropertyComputationColor = 1,
    /** A length, a percentage, or a length-family keyword (`auto`, `normal`). */
    RoboCssPropertyComputationLength = 2,
    /** A single identifier, kept as one (`display`, `position`, `visibility`). */
    RoboCssPropertyComputationKeyword = 3,
    /** A font family list (`font-family`), kept as an ordered list. */
    RoboCssPropertyComputationFontFamily = 4,
    /** `normal`, `bold`, or 100–900 (`font-weight`). */
    RoboCssPropertyComputationFontWeight = 5,
    /** `normal`, a number, a length or a percentage (`line-height`). */
    RoboCssPropertyComputationLineHeight = 6,
    /**
     * A length, a percentage, or `auto` (9.11) — a box edge that may be sized
     * by its container: `margin-top` and its three relatives, and since 9.13 the
     * four inset properties `top`, `right`, `bottom` and `left`.
     *
     * A shape of its own rather than the LENGTH above, and the difference is the
     * *vocabulary* rather than the syntax: `margin-top: normal` is not a margin,
     * and `margin-top: min-content` is not one either, where both are legal for
     * `width`. A shape that accepted every length-family keyword everywhere
     * would make `padding: auto` computable, and CSS says it is a mistake.
     *
     * The inset properties share this shape rather than getting one of their own
     * because `<length-percentage> | auto` *is* the grammar of both families —
     * CSS writes it the same way for `margin-top` and for `top`, and a second
     * shape with the same vocabulary is how the two come to be normalised
     * differently. What differs between them is which property the value belongs
     * to, and that is the registry's question, not the shape's.
     */
    RoboCssPropertyComputationEdgeOffset = 7,
    /**
     * A length or a percentage, and **never** `auto` (9.11) — `padding-top` and
     * its three relatives.
     *
     * The negative of EDGE_OFFSET, and the two exist separately because that is
     * the whole difference between the two families: a padding has nothing to
     * distribute, so there is no `auto` for it to mean. Rejecting it here rather
     * than in a branch at the call site is what makes `padding: auto` an INVALID
     * computed value instead of a keyword that later phases would have to
     * interpret.
     */
    RoboCssPropertyComputationEdgeInset = 8,
    /**
     * A length, or one of the three width keywords (9.11) — the four
     * `border-*-width` longhands.
     *
     * `thin`, `medium` and `thick` are kept as the keywords they are rather than
     * resolved to the lengths they stand for, because their lengths are *used*
     * values: what `medium` is depends on the border style and the device, and a
     * computed value that had already picked a pixel count would be claiming a
     * decision it does not have the context to make.
     */
    RoboCssPropertyComputationLineWidth = 9,
    /**
     * One of the closed display vocabulary (9.12) — `display`.
     *
     * A shape of its own rather than the KEYWORD above, and the difference is
     * what `display` is *for*: a keyword shape carries any identifier through
     * (`cursor: grab`, `position: sticky` — values whose meaning belongs to a
     * phase that has not arrived), while `display` is the property every later
     * phase reads on every element, so `display: bogus` has to be a mistake
     * rather than a keyword somebody downstream will fail to interpret. The
     * vocabulary is closed because the property is closed: `flex` and `grid` are
     * rows this build has not added, not identifiers it has not heard of, and
     * rejecting them is the honest answer until the phases that lay them out
     * exist (RoboCSSDisplay.h).
     */
    RoboCssPropertyComputationDisplay = 10,
    /**
     * One of the closed positioning vocabulary (9.13) — `position`.
     *
     * The same decision the DISPLAY shape above records, for the same reason and
     * a different property: `position` is what every later phase reads to know
     * what a box is positioned *against*, so `position: banana` has to be a
     * mistake rather than a keyword carrying the text `banana`. A keyword shape
     * would carry it through and leave the discovery to whichever phase first
     * dispatches on the value, which is exactly the silent failure the closed
     * vocabularies exist to move forward in time.
     *
     * `position: sticky` is a row here and is *not* a scrolling implementation:
     * the value and its two classifications (in flow, positioned) are what this
     * build can honestly say about it, and what it does relative to a scrollport
     * is the phase that has one.
     */
    RoboCssPropertyComputationPosition = 11,
    /**
     * `auto`, or a whole number (9.13) — `z-index`.
     *
     * Named for its property rather than called INTEGER, because the two halves
     * are the point: `z-index: auto` and `z-index: 0` are *different answers*
     * (one says "this element does not participate in stacking decisions", the
     * other says "at level zero"), so a shape that accepted only integers could
     * not represent the property at all, and a shape that accepted any number
     * would make `z-index: 1.5` computable, which CSS refuses.
     *
     * `1.5` and `1.0` are both refused, and the second is the one worth stating:
     * CSS's `<integer>` is a *grammar* rather than a value that happens to be
     * whole, so a number token is a number token however it is written. The unit
     * module already answers that question exactly once
     * (CssNumericIntegerValue), and this shape asks it rather than
     * restating it.
     */
    RoboCssPropertyComputationZIndex = 12,
    /**
     * One of the closed background-repeat vocabulary (9.14) — `background-repeat`.
     *
     * A row in the shared closed-vocabulary table rather than a fourth copy of
     * "ask the identifier, then ask the table": the vocabulary, its two doors and
     * its refusals are data, so a third and fourth closed vocabulary are two more
     * rows. The set is closed for the same reason `display`'s and `position`'s are
     * — `background-repeat: banana` has to be a mistake rather than a keyword
     * carrying the text `banana` — and `space` and `round`, CSS's other two values,
     * are rows this build has not added rather than words it does not know.
     */
    RoboCssPropertyComputationBackgroundRepeat = 13,
    /**
     * One of the closed background-attachment vocabulary (9.14) —
     * `background-attachment`.
     *
     * Two values, and closed: `local` is a value this build has not added. Two is
     * few enough that a table looks like ceremony and it is not — the alternative
     * is a comparison against one of two spellings in the shape that reads it, and
     * that is where `floating` would become computable.
     */
    RoboCssPropertyComputationBackgroundAttachment = 14,
    /**
     * `none`, or a reference this build carries (9.14) — `background-image`.
     *
     * A shape of its own rather than PRESERVE, because `none` is a *value* here
     * and not a coincidence: it is the property's initial value and it is what a
     * `background:` shorthand resets the image to, so it is typed. Everything else
     * that can be an image — `url(...)`, `linear-gradient(...)`, a bare URL token
     * — is deferred with its tokens intact, which is the answer 9.9 gives a colour
     * space this build cannot represent and a different answer from "invalid":
     * refusing legal CSS is worse than carrying it.
     */
    RoboCssPropertyComputationBackgroundImage = 15,
    /**
     * A pair of components, one per axis (9.14) — `background-position`.
     *
     * The first shape here whose value is neither one enumerator nor one length: a
     * position is *two* components that are each a keyword, a length or a
     * percentage, and the pair is the value because CSS's single-component forms
     * are folded into it (`center` is `center center`). Percentages stay
     * percentages, because 25% of what is a question only the phase with a
     * containing block can answer.
     */
    RoboCssPropertyComputationBackgroundPosition = 16,
    /**
     * A whole-value keyword, or a pair of components (9.14) — `background-size`.
     *
     * `cover` and `contain` are *whole values* rather than components, so unlike
     * the position shape this one is not always a pair — and a pair of two `auto`s
     * is folded back to the keyword, because `auto` and `auto auto` are one
     * computed value with one spelling.
     */
    RoboCssPropertyComputationBackgroundSize = 17,
    /**
     * One of the closed border-style vocabulary (9.15) — the four
     * `border-*-style` longhands.
     *
     * A shape of its own rather than the KEYWORD above, for the reason the display
     * and position vocabularies record: `border-top-style` is a value a paint phase
     * dispatches on (`none` and `hidden` draw nothing, and a border that draws
     * nothing has a *used* width of zero whatever `border-width` says), so
     * `border-top-style: banana` has to be a mistake rather than a keyword
     * carrying the text `banana` that the next phase silently fails to interpret.
     *
     * The style's *colour* and *width* siblings need no shape of their own: a
     * colour is the 9.9 `COLOR` shape and a width the 9.11 `LINE_WIDTH` one, and a
     * second copy of either would be how `border-top-color: currentColor` and
     * `color: currentColor` come to be resolved differently.
     */
    RoboCssPropertyComputationBorderStyle = 18,
    /**
     * One of the closed overflow vocabulary (10.13) — `overflow`, `overflow-x` and
     * `overflow-y`.
     *
     * The same decision the display, position and border-style shapes record, for the
     * same reason and a property family rather than a property: `overflow` is what a
     * layout pass dispatches on — "does this axis clip?" changes geometry — so
     * `overflow: banana` has to be a mistake rather than a keyword carrying the text
     * `banana` that the phase with a box silently fails to interpret.
     *
     * All three names take this shape, and the *shorthand* doing so is deliberate: no
     * phase in this build expands a shorthand, so a `overflow` left on the generic
     * PRESERVE shape would carry tokens nothing reads and a document written as
     * `overflow: hidden` would lay out exactly as if the declaration were absent. The
     * vocabulary is the shorthand's meaning one step early rather than a second
     * implementation of expansion, and `RoboCSSOverflow.h` states the cost: the
     * two-value form (`overflow: hidden visible`) is not read.
     */
    RoboCssPropertyComputationOverflow = 19,
    /**
     * An `opacity`: a number or a percentage, clamped to `[0, 1]` (11.9).
     *
     * The first shape here that is neither a closed vocabulary nor a length family, and the
     * first added by a phase of the *paint* stack rather than by a 9.x value phase or by a
     * layout phase: `opacity` is the one property a painter reads that changes neither the
     * geometry of a box nor its colour, and what it changes is the *strength* at which the box
     * and everything inside it reaches a surface.
     *
     * A shape of its own rather than the NUMBER family, and the difference is the range rather
     * than the syntax: `opacity` is the only property in this build whose value is bounded on
     * both sides, and reading it as a bare number would put the clamp in every consumer
     * instead of in one place. The initial value is `1` rather than `0`, which is the one thing
     * about this row worth reading twice: every other property's initial value is the zero of
     * its own enumerator (`visible`, `static`, `inline`), and a build that let the zeroing of a
     * record stand in for this one would render every element of a document that never mentions
     * `opacity` invisible (RoboCSSOpacity.h argues it).
     */
    RoboCssPropertyComputationOpacity = 20
} CssPropertyComputation;

/** Number of defined computations. */
#define RoboCssPropertyComputationCount 21u

/** Non-zero when @p computation is a defined enumerator. */
int CssPropertyComputationIsKnown(CssPropertyComputation computation);

/** Human-readable name ("preserve", "colour", "length"); never NULL. */
const char *CssPropertyComputationName(
    CssPropertyComputation computation);

/**
 * One registered property.
 *
 * Owned by the registry and handed out borrowed. `name` and `initialText` are
 * NUL-terminated and owned by the definition; `initialValue` is owned by the
 * definition and is KIND_NONE when the property has no registered initial value
 * — which is a real state, not an error: CSS leaves some initial values to the
 * user agent, and inventing one would be worse than saying there is none.
 */
typedef struct CssPropertyDefinition {
    /**
     * NUL-terminated name, owned by the definition. Handed out through a
     * `const` pointer, so it is written only by the registry; the field is not
     * itself const because the registry is the one that frees it.
     */
    char *name;
    /** Length of @p name in bytes, excluding its NUL. */
    size_t nameLength;
    CssPropertyInheritance inheritance;
    /** How this property's value becomes a computed value (9.8). */
    CssPropertyComputation computation;
    /** Bitmask of CssPropertyFlag, for CssPropertyRegistryApplyToTable. */
    uint32_t flags;
    /** Owned, NUL-terminated initial value text; NULL when there is none. */
    char *initialText;
    /** Length of @p initialText in bytes, excluding its NUL. */
    size_t initialTextLength;
    /**
     * Owned parsed initial value; KIND_NONE when @p initialText is NULL.
     *
     * Its slices borrow @p initialText, so the two are freed together and the
     * text outlives the value.
     */
    CssValue initialValue;
} CssPropertyDefinition;

/** Non-zero when @p definition describes an INHERITED property. NULL → 0. */
int CssPropertyDefinitionInherited(
    const CssPropertyDefinition *definition);

/**
 * Borrowed initial value of @p definition, or NULL when it has none.
 * The pointer belongs to the registry and stays valid until it is destroyed.
 */
const CssValue *CssPropertyDefinitionInitialValue(
    const CssPropertyDefinition *definition);

/** An opaque, caller-owned set of property classifications and initial values. */
typedef struct CssPropertyRegistry CssPropertyRegistry;

/**
 * Create an empty registry — no property is classified.
 * @return RoboOk; RoboInvalidArgument for a NULL output; RoboOutOfMemory.
 * The caller owns it until CssPropertyRegistryDestroy.
 */
Result CssPropertyRegistryCreate(CssPropertyRegistry **outRegistry,
                                                CssDiag *diag);

/**
 * Create a registry holding CSS's standard property set for this build.
 *
 * What "standard" means here is exact: the properties these phases have any
 * business knowing — the common inherited properties (colour and the font and
 * text properties), the common non-inherited ones (the box properties,
 * `display`, `position`), and their initial values. It is deliberately not a
 * browser's user-agent stylesheet and not a complete property database: a
 * property family this build does not implement is a property this build should
 * not claim to classify, because a classification is used to decide whether a
 * value propagates, and propagating a value for a property nothing understands
 * is a way to be confidently wrong.
 *
 * A caller can register more; the standard set is a starting point, not a
 * closed list.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL output; RoboOutOfMemory.
 */
Result CssPropertyRegistryCreateStandard(
    CssPropertyRegistry **outRegistry,
    CssDiag *diag);

/** Destroy @p registry and every name, text and initial value it owns. Safe on NULL. */
void CssPropertyRegistryDestroy(CssPropertyRegistry *registry);

/**
 * Register @p name with @p inheritance and an optional @p initialText.
 *
 * @p name is copied and must be a usable property name: non-empty, at most
 * RoboCssPropertyNameMax bytes, with no NUL, whitespace, `:` or `.` — the
 * same rule CssPropertyTableRegister applies, so a registry can be
 * applied to a table without a name being refused there.
 *
 * @p initialText may be NULL, meaning the property's initial value is not
 * known to this build; it can be read back as NULL and
 * CssPropertyDefinitionInitialValue answers NULL. When it is not NULL
 * it is parsed through the real grammar, and a text that does not parse is
 * rejected rather than stored — a broken initial value is a defect in the
 * registry, and it should surface where it was written rather than as a
 * mysteriously absent value much later.
 *
 * Registering a name that is already present is RoboAlreadyExists, and the
 * stored definition is left alone: first registration wins, so a classification
 * cannot change underneath a caller that read it.
 *
 * @return RoboOk; RoboAlreadyExists; RoboInvalidArgument for a NULL
 *         registry, an unusable name, an unknown classification, or an
 *         unparsable initial value; RoboOutOfMemory.
 */
Result CssPropertyRegistryRegister(CssPropertyRegistry *registry,
                                                  const char *name,
                                                  CssPropertyInheritance inheritance,
                                                  const char *initialText,
                                                  uint32_t flags,
                                                  CssDiag *diag);

/**
 * As CssPropertyRegistryRegister, plus the value shape computation needs.
 *
 * The five-argument form is this one with
 * RoboCssPropertyComputationPreserve, so a caller that registers a property
 * for identity and inheritance alone is not forced to take a position on how its
 * value is computed. That is a real position: PRESERVE is what an unclassified
 * property gets, and it is not a placeholder — it is the correct computation for
 * a property whose shape nothing here claims to know.
 *
 * @return the same codes as CssPropertyRegistryRegister, plus
 *         RoboInvalidArgument for an unknown @p computation.
 */
Result CssPropertyRegistryRegisterTyped(
    CssPropertyRegistry *registry,
    const char *name,
    CssPropertyInheritance inheritance,
    CssPropertyComputation computation,
    const char *initialText,
    uint32_t flags,
    CssDiag *diag);

/**
 * Look up a property's classification *by identity* (9.8).
 *
 * The definitions here carry no id — ids belong to a caller's table — so a
 * caller holding an id cannot ask the registry directly. This walks the
 * registry's own order and asks @p table what each definition's name is worth,
 * which is the only sound way to do it: the mapping is the table's, so the
 * question has to go through it.
 *
 * @return a borrowed definition, or NULL when the registry has no definition
 *         whose name the table resolves to @p property.
 */
const CssPropertyDefinition *CssPropertyRegistryFindProperty(
    const CssPropertyRegistry *registry,
    const CssPropertyTable *table,
    CssPropertyId property);

/**
 * Find @p name (NUL-terminated).
 *
 * Comparison follows CSS's own rule: ASCII case-insensitive for an ordinary
 * property name, byte-exact for a custom property (`--x`), since those are
 * case-sensitive. The same rule decides what counts as "the same property" in
 * the inheritance phase, so the two cannot disagree about a name.
 *
 * @return a borrowed definition, or NULL when the registry has no such
 *         property. Valid until the registry is destroyed.
 */
const CssPropertyDefinition *CssPropertyRegistryFind(
    const CssPropertyRegistry *registry,
    const char *name);

/** Slice flavour of the above; a slice holding an embedded NUL never matches. */
const CssPropertyDefinition *CssPropertyRegistryFindSlice(
    const CssPropertyRegistry *registry,
    CssSlice name);

/** The definition at @p index in registration order, or NULL when out of range. */
const CssPropertyDefinition *CssPropertyRegistryAt(
    const CssPropertyRegistry *registry,
    size_t index);

/** Number of registered properties (0 for NULL). */
size_t CssPropertyRegistryCount(const CssPropertyRegistry *registry);

/**
 * Register every property in @p registry into @p table, with
 * RoboCssPropertyFlagInherited set for the ones that inherit by default.
 *
 * This is how a table comes to agree with the registry: the flag a caller's
 * table carries is the registry's classification, so a property cannot be
 * classified one way here and another way there.
 *
 * A property the table already holds is skipped, not an error — the table's
 * existing id is kept and the registry has nothing to say about how the caller
 * numbered its own properties. The count written to @p outRegistered (which
 * may be NULL) is how many were newly registered.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL registry or table;
 *         RoboOutOfMemory when the table could not grow.
 */
Result CssPropertyRegistryApplyToTable(
    const CssPropertyRegistry *registry,
    CssPropertyTable *table,
    size_t *outRegistered,
    CssDiag *diag);

/**
 * Non-zero when @p name is a custom property: it begins with `--`, followed by
 * at least one byte.
 *
 * A property name comparison needs this because custom properties are
 * case-sensitive where ordinary property names are not, and both live in the
 * same signature. It is a *naming* question, so it is answered here beside the
 * other naming rule rather than at each call site.
 */
int CssIsCustomPropertyName(CssSlice name);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_PROPERTY_REGISTRY_H */
