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
 * Four sets meet in robo_css_inherit_resolve, and the order they are walked in
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

#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSSInternal.h"
#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Keywords                                                            */
/* ------------------------------------------------------------------ */

int robo_css_wide_keyword_is_known(robo_css_wide_keyword_t keyword)
{
    return keyword >= ROBO_CSS_WIDE_KEYWORD_NONE &&
           keyword < (robo_css_wide_keyword_t)ROBO_CSS_WIDE_KEYWORD_COUNT;
}

const char *robo_css_wide_keyword_name(robo_css_wide_keyword_t keyword)
{
    switch (keyword) {
    case ROBO_CSS_WIDE_KEYWORD_NONE:
        return "none";
    case ROBO_CSS_WIDE_KEYWORD_INHERIT:
        return "inherit";
    case ROBO_CSS_WIDE_KEYWORD_INITIAL:
        return "initial";
    case ROBO_CSS_WIDE_KEYWORD_UNSET:
        return "unset";
    case ROBO_CSS_WIDE_KEYWORD_REVERT:
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
static const robo_css_token_t *keyword_identifier(const robo_css_value_t *value)
{
    const robo_css_value_t *leaf = value;

    if (!value) {
        return NULL;
    }
    if (value->kind == ROBO_CSS_VALUE_KIND_LIST) {
        if (value->children.count != 1u) {
            return NULL;
        }
        leaf = &value->children.items[0];
    }
    if (leaf->kind != ROBO_CSS_VALUE_KIND_TOKEN ||
        leaf->token.type != ROBO_CSS_TOKEN_IDENT) {
        return NULL;
    }
    return &leaf->token;
}

robo_css_wide_keyword_t robo_css_wide_keyword_of(const robo_css_value_t *value)
{
    const robo_css_token_t *identifier = keyword_identifier(value);

    if (!identifier) {
        return ROBO_CSS_WIDE_KEYWORD_NONE;
    }
    /* Decoded, not raw: an escape changes which bytes spell a name, never which
     * name it is, so `\69 nherit` is the keyword. */
    if (robo_css_token_value_equals_ascii_ci(identifier, "inherit")) {
        return ROBO_CSS_WIDE_KEYWORD_INHERIT;
    }
    if (robo_css_token_value_equals_ascii_ci(identifier, "initial")) {
        return ROBO_CSS_WIDE_KEYWORD_INITIAL;
    }
    if (robo_css_token_value_equals_ascii_ci(identifier, "unset")) {
        return ROBO_CSS_WIDE_KEYWORD_UNSET;
    }
    if (robo_css_token_value_equals_ascii_ci(identifier, "revert")) {
        return ROBO_CSS_WIDE_KEYWORD_REVERT;
    }
    return ROBO_CSS_WIDE_KEYWORD_NONE;
}

/* ------------------------------------------------------------------ */
/* Sources                                                             */
/* ------------------------------------------------------------------ */

int robo_css_inherited_source_is_known(robo_css_inherited_source_t source)
{
    return source >= ROBO_CSS_INHERITED_SOURCE_NONE &&
           source < (robo_css_inherited_source_t)ROBO_CSS_INHERITED_SOURCE_COUNT;
}

const char *robo_css_inherited_source_name(robo_css_inherited_source_t source)
{
    switch (source) {
    case ROBO_CSS_INHERITED_SOURCE_NONE:
        return "none";
    case ROBO_CSS_INHERITED_SOURCE_DECLARED:
        return "declared";
    case ROBO_CSS_INHERITED_SOURCE_PARENT:
        return "parent";
    case ROBO_CSS_INHERITED_SOURCE_DEFAULT:
        return "default";
    case ROBO_CSS_INHERITED_SOURCE_DEFERRED:
        return "deferred";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Entries                                                             */
/* ------------------------------------------------------------------ */

robo_css_slice_t robo_css_inherited_property_name(
    const robo_css_inherited_property_t *property)
{
    if (!property || !property->name) {
        return robo_css_slice_empty();
    }
    return robo_css_slice_from_bytes((const uint8_t *)property->name,
                                     property->name_length);
}

const robo_css_value_t *robo_css_inherited_property_value(
    const robo_css_inherited_property_t *property)
{
    if (!property || property->value.kind == ROBO_CSS_VALUE_KIND_NONE) {
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
static int inherited_entry_matches(const robo_css_inherited_property_t *entry,
                                   robo_css_slice_t name)
{
    robo_css_slice_t stored = robo_css_inherited_property_name(entry);

    if (robo_css_is_custom_property_name(name) ||
        robo_css_is_custom_property_name(stored)) {
        return robo_css_slice_equals(stored, name);
    }
    return robo_css_slice_equals_ci(stored, name);
}

/* ------------------------------------------------------------------ */
/* The resolved style                                                  */
/* ------------------------------------------------------------------ */

void robo_css_inherited_style_init(robo_css_inherited_style_t *style)
{
    if (!style) {
        return;
    }
    style->items = NULL;
    style->count = 0u;
    style->capacity = 0u;
    style->element = ROBO_CSS_ELEMENT_REF_NONE;
    style->has_parent = 0;
    style->declared_count = 0u;
    style->inherited_count = 0u;
    style->default_count = 0u;
    style->deferred_count = 0u;
    style->unresolved_count = 0u;
    style->unsupported = 0;
}

void robo_css_inherited_style_dispose(robo_css_inherited_style_t *style)
{
    size_t index;

    if (!style) {
        return;
    }
    for (index = 0u; index < style->count; index++) {
        robo_css_inherited_property_t *entry = &style->items[index];

        robo_css_value_dispose(&entry->value);
        robo_free(entry->name);
    }
    robo_free(style->items);
    robo_css_inherited_style_init(style);
}

size_t robo_css_inherited_style_count(const robo_css_inherited_style_t *style)
{
    return style ? style->count : 0u;
}

const robo_css_inherited_property_t *robo_css_inherited_style_at(
    const robo_css_inherited_style_t *style,
    size_t index)
{
    if (!style || index >= style->count) {
        return NULL;
    }
    return &style->items[index];
}

const robo_css_inherited_property_t *robo_css_inherited_style_find(
    const robo_css_inherited_style_t *style,
    robo_css_slice_t name)
{
    size_t index;

    if (!style || robo_css_slice_is_absent(name)) {
        return NULL;
    }
    for (index = 0u; index < style->count; index++) {
        if (inherited_entry_matches(&style->items[index], name)) {
            return &style->items[index];
        }
    }
    return NULL;
}

const robo_css_inherited_property_t *robo_css_inherited_style_find_property(
    const robo_css_inherited_style_t *style,
    robo_css_property_id_t property)
{
    size_t index;

    if (!style || property == ROBO_CSS_PROPERTY_ID_NONE) {
        return NULL;
    }
    for (index = 0u; index < style->count; index++) {
        if (style->items[index].property == property) {
            return &style->items[index];
        }
    }
    return NULL;
}

int robo_css_inherited_style_has(const robo_css_inherited_style_t *style,
                                 robo_css_slice_t name)
{
    return robo_css_inherited_style_find(style, name) != NULL;
}

int robo_css_inherited_style_is_complete(const robo_css_inherited_style_t *style)
{
    return style && !style->unsupported;
}

robo_css_element_ref_t robo_css_inherited_style_element(
    const robo_css_inherited_style_t *style)
{
    return style ? style->element : ROBO_CSS_ELEMENT_REF_NONE;
}

int robo_css_inherited_style_has_parent(const robo_css_inherited_style_t *style)
{
    return style ? style->has_parent : 0;
}

size_t robo_css_inherited_style_shorthands_unexpanded(
    const robo_css_inherited_style_t *style)
{
    return style ? style->shorthands_unexpanded : 0u;
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

/**
 * One entry as it is built. The name is copied from whatever named the
 * property, because a resolved style must not borrow its identity from a sheet
 * that may be destroyed while the style lives.
 */
static int inherit_append(robo_css_inherited_style_t *style,
                          robo_css_property_id_t property,
                          robo_css_slice_t name,
                          robo_css_inherited_source_t source,
                          robo_css_wide_keyword_t keyword,
                          const robo_css_value_t *value)
{
    robo_css_inherited_property_t *slot;
    char *name_copy;

    /* The name first, so a failure leaves the style exactly as it was. */
    name_copy = (char *)robo_malloc(name.length + 1u);
    if (!name_copy) {
        return 0;
    }
    if (name.length > 0u) {
        memcpy(name_copy, name.bytes, name.length);
    }
    name_copy[name.length] = '\0';

    if (!robo_css_grow((void **)&style->items, &style->capacity, style->count,
                       sizeof(robo_css_inherited_property_t))) {
        robo_free(name_copy);
        return 0;
    }
    slot = &style->items[style->count];
    memset(slot, 0, sizeof(*slot));
    robo_css_value_init(&slot->value);
    slot->property = property;
    slot->name = name_copy;
    slot->name_length = name.length;
    slot->source = source;
    slot->keyword = keyword;

    if (value && value->kind != ROBO_CSS_VALUE_KIND_NONE) {
        if (robo_css_value_copy(&slot->value, value, NULL) != ROBO_OK) {
            robo_free(name_copy);
            return 0;
        }
    } else {
        /* No value to carry: the typed "I do not know this property's default"
         * state, which is a different fact from having no entry. */
        slot->unresolved = 1;
        style->unresolved_count++;
        style->unsupported = 1;
    }
    style->count++;
    return 1;
}

/** Count one entry into the tally its source belongs to. */
static void inherit_tally(robo_css_inherited_style_t *style,
                          robo_css_inherited_source_t source)
{
    switch (source) {
    case ROBO_CSS_INHERITED_SOURCE_DECLARED:
        style->declared_count++;
        break;
    case ROBO_CSS_INHERITED_SOURCE_PARENT:
        style->inherited_count++;
        break;
    case ROBO_CSS_INHERITED_SOURCE_DEFAULT:
        style->default_count++;
        break;
    case ROBO_CSS_INHERITED_SOURCE_DEFERRED:
        style->deferred_count++;
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
 *   3. otherwise the caller's property table, whose ROBO_CSS_PROPERTY_FLAG_INHERITED
 *      bit exists for exactly this (its own header has said "9.7 reads this"
 *      since 9.1);
 *   4. otherwise it does not inherit.
 *
 * Step 4 is a decision rather than a silence. CSS drops a declaration for a
 * property it does not recognise, so a property nothing here recognises is not
 * one whose value should propagate down a tree: inheriting it would spread a
 * value no phase can interpret into every descendant.
 */
static int inherit_classify(const robo_css_style_context_t *context,
                            robo_css_slice_t name)
{
    const robo_css_property_registry_t *registry =
        robo_css_style_context_property_registry(context);
    const robo_css_property_definition_t *definition;
    const robo_css_property_table_t *table;

    if (robo_css_is_custom_property_name(name)) {
        return 1;
    }
    definition = robo_css_property_registry_find_slice(registry, name);
    if (definition) {
        return robo_css_property_definition_inherited(definition);
    }
    table = robo_css_style_context_property_table(context);
    if (table) {
        robo_css_property_info_t info = ROBO_CSS_CLEARED;

        if (robo_css_property_table_info(table,
                                         robo_css_property_table_lookup_slice(table, name),
                                         &info, NULL) == ROBO_OK) {
            return robo_css_property_flags_inherited(info.flags);
        }
    }
    return 0;
}

/** The registry's initial value for @p name, or NULL when there is none. */
static const robo_css_value_t *inherit_default_value(
    const robo_css_style_context_t *context,
    robo_css_slice_t name)
{
    const robo_css_property_definition_t *definition =
        robo_css_property_registry_find_slice(
            robo_css_style_context_property_registry(context), name);

    /* A custom property has no initial value in CSS — an unset one is invalid,
     * not `initial` — so it is never defaulted, whatever a registry might say. */
    if (!definition || robo_css_is_custom_property_name(name)) {
        return NULL;
    }
    return robo_css_property_definition_initial_value(definition);
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
static const robo_css_inherited_property_t *inherit_parent_entry(
    const robo_css_inherited_style_t *parent,
    robo_css_slice_t name)
{
    const robo_css_inherited_property_t *entry;

    if (!parent) {
        return NULL;
    }
    entry = robo_css_inherited_style_find(parent, name);
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
static int inherit_value(robo_css_inherited_style_t *out,
                         const robo_css_style_context_t *context,
                         robo_css_property_id_t property,
                         robo_css_slice_t name,
                         const robo_css_value_t *value,
                         const robo_css_inherited_style_t *parent,
                         int inherited_by_default)
{
    robo_css_wide_keyword_t keyword = robo_css_wide_keyword_of(value);
    const robo_css_inherited_property_t *from_parent;

    if (keyword == ROBO_CSS_WIDE_KEYWORD_REVERT) {
        /* Recognised, and deliberately not obeyed: see the header. The keyword
         * is carried as the value so nothing is lost, and the entry is counted
         * and marked deferred so nothing reads it as an answer. */
        if (!inherit_append(out, property, name,
                            ROBO_CSS_INHERITED_SOURCE_DEFERRED, keyword, value)) {
            return 0;
        }
        out->unsupported = 1;
        inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_DEFERRED);
        return 1;
    }

    if (keyword == ROBO_CSS_WIDE_KEYWORD_INHERIT ||
        (keyword == ROBO_CSS_WIDE_KEYWORD_UNSET && inherited_by_default)) {
        from_parent = inherit_parent_entry(parent, name);
        if (from_parent &&
            inherit_append(out, property, name,
                           ROBO_CSS_INHERITED_SOURCE_PARENT, keyword,
                           &from_parent->value)) {
            inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_PARENT);
            return 1;
        }
        /* `inherit` with no parent value is the parent's *computed* value, and
         * an undeclared non-inherited property's computed value is its initial
         * one — so the default is the right answer, not a failure. */
        if (!inherit_append(out, property, name,
                            ROBO_CSS_INHERITED_SOURCE_DEFAULT, keyword,
                            inherit_default_value(context, name))) {
            return 0;
        }
        inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_DEFAULT);
        return 1;
    }

    if (keyword == ROBO_CSS_WIDE_KEYWORD_INITIAL ||
        (keyword == ROBO_CSS_WIDE_KEYWORD_UNSET && !inherited_by_default)) {
        if (!inherit_append(out, property, name,
                            ROBO_CSS_INHERITED_SOURCE_DEFAULT, keyword,
                            inherit_default_value(context, name))) {
            return 0;
        }
        inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_DEFAULT);
        return 1;
    }

    if (!inherit_append(out, property, name,
                        ROBO_CSS_INHERITED_SOURCE_DECLARED,
                        ROBO_CSS_WIDE_KEYWORD_NONE, value)) {
        return 0;
    }
    inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_DECLARED);
    return 1;
}

/**
 * Resolve one property whose winning declaration the cascade already chose.
 *
 * The one-candidate spelling of the above, kept because that is how every
 * property that is not a box-model longhand arrives.
 */
static int inherit_declared(robo_css_inherited_style_t *out,
                            const robo_css_style_context_t *context,
                            const robo_css_cascade_candidate_t *declared,
                            const robo_css_inherited_style_t *parent,
                            robo_css_slice_t name,
                            int inherited_by_default)
{
    return inherit_value(out, context, declared->property, name,
                         robo_css_declaration_value(declared->declaration), parent,
                         inherited_by_default);
}

/**
 * Carry @p entry from the parent, because @p name inherits by default and the
 * element did not declare it.
 */
static int inherit_from_parent(robo_css_inherited_style_t *out,
                               robo_css_property_id_t property,
                               robo_css_slice_t name,
                               const robo_css_inherited_property_t *entry)
{
    robo_css_value_t value = ROBO_CSS_CLEARED;
    int appended;

    /* A deferred keyword the parent carried is carried down as one, so the
     * author's declaration is never silently lost on the way; everything else
     * is copied as a value. */
    if (entry->source == ROBO_CSS_INHERITED_SOURCE_DEFERRED) {
        appended = inherit_append(out, property, name,
                                  ROBO_CSS_INHERITED_SOURCE_DEFERRED,
                                  entry->keyword, &entry->value);
        if (appended) {
            out->unsupported = 1;
            inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_DEFERRED);
        }
        return appended;
    }

    value = entry->value;
    appended = inherit_append(out, property, name, ROBO_CSS_INHERITED_SOURCE_PARENT,
                              ROBO_CSS_WIDE_KEYWORD_NONE, &value);
    if (appended) {
        inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_PARENT);
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
 * (`robo_css_cascade_candidate_compare`) rather than answered here. Nothing in
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
static int inherit_expand_box_shorthands(robo_css_inherited_style_t *out,
                                        const robo_css_style_context_t *context,
                                        const robo_css_cascade_t *cascade,
                                        const robo_css_inherited_style_t *parent)
{
    const robo_css_property_table_t *table;
    size_t row_index;

    if (!cascade) {
        return 1;
    }
    table = robo_css_style_context_property_table(context);
    for (row_index = 0u; row_index < robo_css_box_shorthand_count(); row_index++) {
        const robo_css_box_shorthand_t *shorthand =
            robo_css_box_shorthand_at(row_index);
        const robo_css_cascade_candidate_t *shorthand_winner = NULL;
        const robo_css_value_t *shorthand_value = NULL;
        robo_css_wide_keyword_t shorthand_keyword = ROBO_CSS_WIDE_KEYWORD_NONE;
        size_t component_count = 0u;
        int expandable = 0;
        size_t side_index;

        (void)robo_css_cascade_resolve_name(
            cascade, robo_css_slice_from_text(shorthand->name), &shorthand_winner);
        if (shorthand_winner) {
            shorthand_value =
                robo_css_declaration_value(shorthand_winner->declaration);
            shorthand_keyword = robo_css_wide_keyword_of(shorthand_value);
            if (shorthand_keyword == ROBO_CSS_WIDE_KEYWORD_NONE) {
                expandable = robo_css_box_is_expandable(shorthand_value,
                                                        &component_count);
                if (!expandable) {
                    /* Counted once for the shorthand rather than once per side,
                     * because it is one declaration that went undistributed. */
                    out->shorthands_unexpanded++;
                    out->unsupported = 1;
                }
            }
        }

        for (side_index = 0u; side_index < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side_index++) {
            robo_css_box_side_t side = (robo_css_box_side_t)side_index;
            const char *longhand_name = robo_css_box_shorthand_longhand(shorthand, side);
            robo_css_slice_t name = robo_css_slice_from_text(longhand_name);
            const robo_css_cascade_candidate_t *longhand_winner = NULL;
            robo_css_property_id_t property;
            int inherited_by_default;
            int longhand_wins;

            (void)robo_css_cascade_resolve_name(cascade, name, &longhand_winner);
            longhand_wins = longhand_winner != NULL &&
                            (!shorthand_winner ||
                             robo_css_cascade_candidate_compare(cascade,
                                                                shorthand_winner,
                                                                longhand_winner) > 0);
            if (!longhand_wins && !shorthand_winner) {
                /* Neither spoke for this edge: it has no entry, and it is not
                 * supposed to — see set 3 in the header comment. */
                continue;
            }
            property = table ? robo_css_property_table_lookup_slice(table, name)
                             : ROBO_CSS_PROPERTY_ID_NONE;
            inherited_by_default = inherit_classify(context, name);

            if (longhand_wins) {
                if (!inherit_value(out, context, longhand_winner->property, name,
                                   robo_css_declaration_value(longhand_winner->declaration),
                                   parent, inherited_by_default)) {
                    return 0;
                }
                continue;
            }
            if (shorthand_keyword != ROBO_CSS_WIDE_KEYWORD_NONE) {
                /* The keyword itself, distributed: `inherit` means the parent's
                 * value for the edge, which is the same sentence `inherit_value`
                 * already knows how to obey. */
                if (!inherit_value(out, context, property, name, shorthand_value,
                                   parent, inherited_by_default)) {
                    return 0;
                }
                continue;
            }
            if (expandable) {
                robo_css_value_t component = ROBO_CSS_CLEARED;

                if (robo_css_box_expand_side(shorthand_value, component_count, side,
                                             &component, NULL) != ROBO_OK) {
                    return 0;
                }
                if (!inherit_value(out, context, property, name, &component, parent,
                                   inherited_by_default)) {
                    robo_css_value_dispose(&component);
                    return 0;
                }
                robo_css_value_dispose(&component);
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
 * asked here through `robo_css_cascade_candidate_compare` exactly as the box pass
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
static int inherit_expand_background_shorthand(robo_css_inherited_style_t *out,
                                               const robo_css_style_context_t *context,
                                               const robo_css_cascade_t *cascade,
                                               const robo_css_inherited_style_t *parent)
{
    const robo_css_property_table_t *table;
    const robo_css_cascade_candidate_t *shorthand_winner = NULL;
    const robo_css_value_t *shorthand_value = NULL;
    robo_css_wide_keyword_t shorthand_keyword = ROBO_CSS_WIDE_KEYWORD_NONE;
    robo_css_value_t parts[ROBO_CSS_BACKGROUND_LONGHAND_COUNT];
    int expandable = 0;
    int failed = 0;
    size_t index;

    for (index = 0u; index < ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_value_init(&parts[index]);
    }
    if (!cascade) {
        return 1;
    }
    table = robo_css_style_context_property_table(context);
    (void)robo_css_cascade_resolve_name(
        cascade, robo_css_slice_from_text(robo_css_background_shorthand_name()),
        &shorthand_winner);
    if (shorthand_winner) {
        shorthand_value = robo_css_declaration_value(shorthand_winner->declaration);
        shorthand_keyword = robo_css_wide_keyword_of(shorthand_value);
        if (shorthand_keyword == ROBO_CSS_WIDE_KEYWORD_NONE) {
            /* Distributed once for the whole shorthand rather than once per
             * longhand, because the parts are shared by all six. */
            expandable = robo_css_background_expand(shorthand_value, parts, NULL) ==
                         ROBO_OK;
            if (!expandable) {
                out->shorthands_unexpanded++;
                out->unsupported = 1;
            }
        }
    }

    for (index = 0u; index < ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_slice_t name = robo_css_slice_from_text(
            robo_css_background_longhand_name(index));
        const robo_css_cascade_candidate_t *longhand_winner = NULL;
        robo_css_property_id_t property;
        int inherited_by_default;
        int longhand_wins;

        (void)robo_css_cascade_resolve_name(cascade, name, &longhand_winner);
        longhand_wins = longhand_winner != NULL &&
                        (!shorthand_winner ||
                         robo_css_cascade_candidate_compare(cascade, shorthand_winner,
                                                            longhand_winner) > 0);
        if (!longhand_wins && !shorthand_winner) {
            /* Neither spoke for this longhand: it has no entry and is not
             * supposed to have one. */
            continue;
        }
        property = table ? robo_css_property_table_lookup_slice(table, name)
                         : ROBO_CSS_PROPERTY_ID_NONE;
        inherited_by_default = inherit_classify(context, name);

        if (longhand_wins) {
            if (!inherit_value(out, context, longhand_winner->property, name,
                               robo_css_declaration_value(longhand_winner->declaration),
                               parent, inherited_by_default)) {
                failed = 1;
                break;
            }
            continue;
        }
        if (shorthand_keyword != ROBO_CSS_WIDE_KEYWORD_NONE) {
            /* The keyword itself, distributed: `background: inherit` means the
             * same sentence six times, which is what `inherit_value` already
             * knows how to obey. */
            if (!inherit_value(out, context, property, name, shorthand_value, parent,
                               inherited_by_default)) {
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
        if (parts[index].kind == ROBO_CSS_VALUE_KIND_NONE) {
            /* Present in the shorthand and absent from its components: the longhand
             * is reset to the property's own initial value, and the registry is the
             * one place that value is written down. */
            const robo_css_value_t *initial = inherit_default_value(context, name);

            if (!initial) {
                continue;
            }
            if (!inherit_value(out, context, property, name, initial, parent,
                               inherited_by_default)) {
                failed = 1;
                break;
            }
            continue;
        }
        if (!inherit_value(out, context, property, name, &parts[index], parent,
                           inherited_by_default)) {
            failed = 1;
            break;
        }
    }

    for (index = 0u; index < ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_value_dispose(&parts[index]);
    }
    return failed ? 0 : 1;
}

robo_result_t robo_css_inherit_resolve(const robo_css_style_context_t *context,
                                       const robo_css_cascade_t *cascade,
                                       const robo_css_inherited_style_t *parent,
                                       robo_css_inherited_style_t *out,
                                       robo_css_diag_t *diag)
{
    size_t index;

    if (!context || !out) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_INHERIT, ROBO_INVALID_ARGUMENT,
                             "context or output is NULL");
    }
    robo_css_inherited_style_init(out);
    out->element = cascade ? cascade->element : ROBO_CSS_ELEMENT_REF_NONE;
    out->has_parent = parent != NULL;

    /* The whole pass is transactional: any failure disposes what it built, so a
     * caller that ignores the result code owns an empty style rather than half
     * of one. */
    for (index = 0u; index < (cascade ? cascade->count : 0u); index++) {
        const robo_css_cascade_candidate_t *candidate = &cascade->items[index];
        const robo_css_declaration_t *declaration = candidate->declaration;
        robo_css_slice_t name;
        int inherited_by_default;
        const robo_css_cascade_candidate_t *winner = NULL;

        if (!robo_css_declaration_is_usable(declaration)) {
            continue;
        }
        name = robo_css_declaration_name(declaration);
        if (robo_css_slice_is_absent(name)) {
            continue;
        }
        if (robo_css_box_shorthand_of_longhand(name, NULL) != NULL) {
            /* A box-model longhand is distributed out of its shorthand rather
             * than resolved one name at a time — it and the shorthand compete
             * for the same edge, and the pass below is where that is settled.
             * Materialising it here as well would give the edge two entries. */
            continue;
        }
        if (robo_css_background_is_longhand(name)) {
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
        if (candidate->property != ROBO_CSS_PROPERTY_ID_NONE) {
            (void)robo_css_cascade_resolve_property(cascade, candidate->property,
                                                    &winner);
        } else {
            (void)robo_css_cascade_resolve_name(cascade, name, &winner);
        }
        if (winner != candidate) {
            continue;
        }
        /* A key already materialised is not materialised twice: the winner is
         * unique per property, so a repeat here means the same *name* reached
         * the cascade through two candidates that resolved to no id and differ
         * in case. */
        if (robo_css_inherited_style_has(out, name)) {
            continue;
        }
        inherited_by_default = inherit_classify(context, name);
        if (!inherit_declared(out, context, candidate, parent, name,
                              inherited_by_default)) {
            robo_css_inherited_style_dispose(out);
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_INHERIT, ROBO_OUT_OF_MEMORY,
                                 "the resolved style could not be built");
        }
    }

    /* The box model's longhands, over the shorthands that stand for them. */
    if (!inherit_expand_box_shorthands(out, context, cascade, parent)) {
        robo_css_inherited_style_dispose(out);
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_INHERIT, ROBO_OUT_OF_MEMORY,
                             "the resolved style could not be built");
    }
    /* And the background family's (9.14), which is a pass of its own because its
     * shorthand's components map to longhands by kind rather than by position. */
    if (!inherit_expand_background_shorthand(out, context, cascade, parent)) {
        robo_css_inherited_style_dispose(out);
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_INHERIT, ROBO_OUT_OF_MEMORY,
                             "the resolved style could not be built");
    }
    if (out->shorthands_unexpanded > 0u) {
        /* Reported rather than silent, because this is the one quiet failure in
         * the pass: the shorthand's own entry is intact, so a caller reading the
         * style sees a declaration and no longhands and has no way to tell that a
         * box's four edges or a background's six parts went unanswered. One line
         * per resolution that met such a declaration, in the same spirit as the
         * tokenizer's error line. */
        robo_log_debug("robo_css: inherit: %llu shorthand(s) met that this build "
                       "cannot distribute over their longhands",
                       (unsigned long long)out->shorthands_unexpanded);
    } else {
        robo_log_debug("robo_css: inherit: every shorthand distributed, %llu "
                       "longhand(s) materialised",
                       (unsigned long long)out->count);
    }

    /* What the parent carries, for the properties that inherit by default. */
    for (index = 0u; index < (parent ? parent->count : 0u); index++) {
        const robo_css_inherited_property_t *entry = &parent->items[index];
        robo_css_slice_t name = robo_css_inherited_property_name(entry);

        if (robo_css_slice_is_absent(name) ||
            robo_css_inherited_style_has(out, name)) {
            continue;
        }
        if (!inherit_classify(context, name)) {
            continue;
        }
        if (entry->unresolved) {
            /* The parent had no value either. Nothing is carried and nothing is
             * invented; set 3 below will reach the same conclusion. */
            continue;
        }
        if (!inherit_from_parent(out, entry->property, name, entry)) {
            robo_css_inherited_style_dispose(out);
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_INHERIT, ROBO_OUT_OF_MEMORY,
                                 "the resolved style could not be built");
        }
    }

    /* Every inherited-by-default property the registry knows that neither the
     * element nor the parent supplied: its initial value, or the typed
     * "default not known" state. This is where a root's inherited properties
     * come from, and it is why an element with no declarations at all still
     * resolves to a complete style. */
    {
        const robo_css_property_registry_t *registry =
            robo_css_style_context_property_registry(context);
        size_t registry_count = robo_css_property_registry_count(registry);

        for (index = 0u; index < registry_count; index++) {
            const robo_css_property_definition_t *definition =
                robo_css_property_registry_at(registry, index);
            robo_css_slice_t name;
            robo_css_property_id_t id = ROBO_CSS_PROPERTY_ID_NONE;

            if (!definition || !robo_css_property_definition_inherited(definition)) {
                continue;
            }
            name = robo_css_slice_from_text(definition->name);
            if (robo_css_inherited_style_has(out, name)) {
                continue;
            }
            /* The id, when the context's table knows the name: the entry should
             * be as identity-carrying as a declared one, so a caller holding an
             * id finds it. */
            {
                const robo_css_property_table_t *table =
                    robo_css_style_context_property_table(context);

                if (table) {
                    id = robo_css_property_table_lookup_slice(table, name);
                }
            }
            if (!inherit_append(out, id, name, ROBO_CSS_INHERITED_SOURCE_DEFAULT,
                                ROBO_CSS_WIDE_KEYWORD_NONE,
                                robo_css_property_definition_initial_value(definition))) {
                robo_css_inherited_style_dispose(out);
                return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_INHERIT, ROBO_OUT_OF_MEMORY,
                                     "the resolved style could not be built");
            }
            inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_DEFAULT);
        }
    }

    /* Incompleteness is inherited the way values are: a cascade that could not
     * decide a selector might have declared a property that is about to be
     * taken from the parent instead, and a parent that is itself incomplete
     * makes this element's inherited values no better. */
    if ((cascade && !robo_css_cascade_is_complete(cascade)) ||
        (parent && !robo_css_inherited_style_is_complete(parent))) {
        out->unsupported = 1;
    }
    return ROBO_OK;
}
