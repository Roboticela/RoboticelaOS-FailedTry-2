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
 *   robo_css_property_table_t      identity, per caller, filled by the caller
 *   robo_css_property_registry_t   classification + initial values, CSS's own
 *
 * They meet through robo_css_property_registry_apply_to_table, which registers
 * every property the registry knows into a caller's table with the right
 * ROBO_CSS_PROPERTY_FLAG_INHERITED bit. That is the supported way to build a
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
 * expands shorthands, and nothing here does — which is why ROBO_CSS_PROPERTY_FLAG_SHORTHAND
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
typedef enum robo_css_property_inheritance {
    /** No classification: not registered here. */
    ROBO_CSS_PROPERTY_INHERITANCE_UNKNOWN = 0,
    /** Inherits by default (`color`, `font-size`, every custom property). */
    ROBO_CSS_PROPERTY_INHERITANCE_INHERITED = 1,
    /** Does not inherit (`width`, `display`, `margin`). */
    ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED = 2
} robo_css_property_inheritance_t;

/** Number of defined classifications (not counting UNKNOWN). */
#define ROBO_CSS_PROPERTY_INHERITANCE_COUNT 3u

/** Non-zero when @p inheritance is a defined enumerator. */
int robo_css_property_inheritance_is_known(robo_css_property_inheritance_t inheritance);

/** Human-readable classification name ("inherited", "non-inherited"); never NULL. */
const char *robo_css_property_inheritance_name(
    robo_css_property_inheritance_t inheritance);

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
typedef enum robo_css_property_computation {
    /** Carry the token sequence structurally; interpret nothing. */
    ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE = 0,
    /** A colour: named, `transparent`, hex or `rgb()`/`rgba()`. */
    ROBO_CSS_PROPERTY_COMPUTATION_COLOR = 1,
    /** A length, a percentage, or a length-family keyword (`auto`, `normal`). */
    ROBO_CSS_PROPERTY_COMPUTATION_LENGTH = 2,
    /** A single identifier, kept as one (`display`, `position`, `visibility`). */
    ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD = 3,
    /** A font family list (`font-family`), kept as an ordered list. */
    ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY = 4,
    /** `normal`, `bold`, or 100–900 (`font-weight`). */
    ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT = 5,
    /** `normal`, a number, a length or a percentage (`line-height`). */
    ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT = 6,
    /**
     * A length, a percentage, or `auto` (9.11) — a box edge that may be sized
     * by its container: `margin-top` and its three relatives.
     *
     * A shape of its own rather than the LENGTH above, and the difference is the
     * *vocabulary* rather than the syntax: `margin-top: normal` is not a margin,
     * and `margin-top: min-content` is not one either, where both are legal for
     * `width`. A shape that accepted every length-family keyword everywhere
     * would make `padding: auto` computable, and CSS says it is a mistake.
     */
    ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET = 7,
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
    ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET = 8,
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
    ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH = 9,
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
    ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY = 10
} robo_css_property_computation_t;

/** Number of defined computations. */
#define ROBO_CSS_PROPERTY_COMPUTATION_COUNT 11u

/** Non-zero when @p computation is a defined enumerator. */
int robo_css_property_computation_is_known(robo_css_property_computation_t computation);

/** Human-readable name ("preserve", "colour", "length"); never NULL. */
const char *robo_css_property_computation_name(
    robo_css_property_computation_t computation);

/**
 * One registered property.
 *
 * Owned by the registry and handed out borrowed. `name` and `initial_text` are
 * NUL-terminated and owned by the definition; `initial_value` is owned by the
 * definition and is KIND_NONE when the property has no registered initial value
 * — which is a real state, not an error: CSS leaves some initial values to the
 * user agent, and inventing one would be worse than saying there is none.
 */
typedef struct robo_css_property_definition {
    /**
     * NUL-terminated name, owned by the definition. Handed out through a
     * `const` pointer, so it is written only by the registry; the field is not
     * itself const because the registry is the one that frees it.
     */
    char *name;
    /** Length of @p name in bytes, excluding its NUL. */
    size_t name_length;
    robo_css_property_inheritance_t inheritance;
    /** How this property's value becomes a computed value (9.8). */
    robo_css_property_computation_t computation;
    /** Bitmask of robo_css_property_flag_t, for robo_css_property_registry_apply_to_table. */
    uint32_t flags;
    /** Owned, NUL-terminated initial value text; NULL when there is none. */
    char *initial_text;
    /** Length of @p initial_text in bytes, excluding its NUL. */
    size_t initial_text_length;
    /**
     * Owned parsed initial value; KIND_NONE when @p initial_text is NULL.
     *
     * Its slices borrow @p initial_text, so the two are freed together and the
     * text outlives the value.
     */
    robo_css_value_t initial_value;
} robo_css_property_definition_t;

/** Non-zero when @p definition describes an INHERITED property. NULL → 0. */
int robo_css_property_definition_inherited(
    const robo_css_property_definition_t *definition);

/**
 * Borrowed initial value of @p definition, or NULL when it has none.
 * The pointer belongs to the registry and stays valid until it is destroyed.
 */
const robo_css_value_t *robo_css_property_definition_initial_value(
    const robo_css_property_definition_t *definition);

/** An opaque, caller-owned set of property classifications and initial values. */
typedef struct robo_css_property_registry robo_css_property_registry_t;

/**
 * Create an empty registry — no property is classified.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL output; ROBO_OUT_OF_MEMORY.
 * The caller owns it until robo_css_property_registry_destroy.
 */
robo_result_t robo_css_property_registry_create(robo_css_property_registry_t **out_registry,
                                                robo_css_diag_t *diag);

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
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL output; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_css_property_registry_create_standard(
    robo_css_property_registry_t **out_registry,
    robo_css_diag_t *diag);

/** Destroy @p registry and every name, text and initial value it owns. Safe on NULL. */
void robo_css_property_registry_destroy(robo_css_property_registry_t *registry);

/**
 * Register @p name with @p inheritance and an optional @p initial_text.
 *
 * @p name is copied and must be a usable property name: non-empty, at most
 * ROBO_CSS_PROPERTY_NAME_MAX bytes, with no NUL, whitespace, `:` or `.` — the
 * same rule robo_css_property_table_register applies, so a registry can be
 * applied to a table without a name being refused there.
 *
 * @p initial_text may be NULL, meaning the property's initial value is not
 * known to this build; it can be read back as NULL and
 * robo_css_property_definition_initial_value answers NULL. When it is not NULL
 * it is parsed through the real grammar, and a text that does not parse is
 * rejected rather than stored — a broken initial value is a defect in the
 * registry, and it should surface where it was written rather than as a
 * mysteriously absent value much later.
 *
 * Registering a name that is already present is ROBO_ALREADY_EXISTS, and the
 * stored definition is left alone: first registration wins, so a classification
 * cannot change underneath a caller that read it.
 *
 * @return ROBO_OK; ROBO_ALREADY_EXISTS; ROBO_INVALID_ARGUMENT for a NULL
 *         registry, an unusable name, an unknown classification, or an
 *         unparsable initial value; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_css_property_registry_register(robo_css_property_registry_t *registry,
                                                  const char *name,
                                                  robo_css_property_inheritance_t inheritance,
                                                  const char *initial_text,
                                                  uint32_t flags,
                                                  robo_css_diag_t *diag);

/**
 * As robo_css_property_registry_register, plus the value shape computation needs.
 *
 * The five-argument form is this one with
 * ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, so a caller that registers a property
 * for identity and inheritance alone is not forced to take a position on how its
 * value is computed. That is a real position: PRESERVE is what an unclassified
 * property gets, and it is not a placeholder — it is the correct computation for
 * a property whose shape nothing here claims to know.
 *
 * @return the same codes as robo_css_property_registry_register, plus
 *         ROBO_INVALID_ARGUMENT for an unknown @p computation.
 */
robo_result_t robo_css_property_registry_register_typed(
    robo_css_property_registry_t *registry,
    const char *name,
    robo_css_property_inheritance_t inheritance,
    robo_css_property_computation_t computation,
    const char *initial_text,
    uint32_t flags,
    robo_css_diag_t *diag);

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
const robo_css_property_definition_t *robo_css_property_registry_find_property(
    const robo_css_property_registry_t *registry,
    const robo_css_property_table_t *table,
    robo_css_property_id_t property);

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
const robo_css_property_definition_t *robo_css_property_registry_find(
    const robo_css_property_registry_t *registry,
    const char *name);

/** Slice flavour of the above; a slice holding an embedded NUL never matches. */
const robo_css_property_definition_t *robo_css_property_registry_find_slice(
    const robo_css_property_registry_t *registry,
    robo_css_slice_t name);

/** The definition at @p index in registration order, or NULL when out of range. */
const robo_css_property_definition_t *robo_css_property_registry_at(
    const robo_css_property_registry_t *registry,
    size_t index);

/** Number of registered properties (0 for NULL). */
size_t robo_css_property_registry_count(const robo_css_property_registry_t *registry);

/**
 * Register every property in @p registry into @p table, with
 * ROBO_CSS_PROPERTY_FLAG_INHERITED set for the ones that inherit by default.
 *
 * This is how a table comes to agree with the registry: the flag a caller's
 * table carries is the registry's classification, so a property cannot be
 * classified one way here and another way there.
 *
 * A property the table already holds is skipped, not an error — the table's
 * existing id is kept and the registry has nothing to say about how the caller
 * numbered its own properties. The count written to @p out_registered (which
 * may be NULL) is how many were newly registered.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL registry or table;
 *         ROBO_OUT_OF_MEMORY when the table could not grow.
 */
robo_result_t robo_css_property_registry_apply_to_table(
    const robo_css_property_registry_t *registry,
    robo_css_property_table_t *table,
    size_t *out_registered,
    robo_css_diag_t *diag);

/**
 * Non-zero when @p name is a custom property: it begins with `--`, followed by
 * at least one byte.
 *
 * A property name comparison needs this because custom properties are
 * case-sensitive where ordinary property names are not, and both live in the
 * same signature. It is a *naming* question, so it is answered here beside the
 * other naming rule rather than at each call site.
 */
int robo_css_is_custom_property_name(robo_css_slice_t name);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_PROPERTY_REGISTRY_H */
