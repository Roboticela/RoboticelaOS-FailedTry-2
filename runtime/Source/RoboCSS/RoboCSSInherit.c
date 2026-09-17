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
 * Three sets meet in robo_css_inherit_resolve, and the order they are walked in
 * is the order they are reported in:
 *
 *   1. the properties the element's own cascade declares — one entry each,
 *      taking the *winner* through the cascade's own resolver rather than
 *      re-deciding precedence here;
 *   2. the properties that inherit by default and that the parent carries;
 *   3. the properties that inherit by default that neither of the above
 *      supplied, resolved to their initial value.
 *
 * Set 3 is what a root element gets, and what a caller who passes no parent is
 * asking for. It is deliberately *only* the inherited-by-default properties:
 * materialising `width: auto` on every element because nobody declared it would
 * put a value in a resolution that no input produced, and 9.8 can reach the
 * registry's initial value for that.
 */

#include "RoboCSS/RoboCSSInherit.h"

#include "RoboCSS/RoboCSSError.h"
#include "RoboCSSInternal.h"
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
 * Resolve one property that the element's cascade declares.
 *
 * `declared` is the winner the *cascade* chose, so nothing here re-decides
 * precedence: this function only obeys the CSS-wide keyword the winner carried,
 * or copies the value.
 */
static int inherit_declared(robo_css_inherited_style_t *out,
                            const robo_css_style_context_t *context,
                            const robo_css_cascade_candidate_t *declared,
                            const robo_css_inherited_style_t *parent,
                            robo_css_slice_t name,
                            int inherited_by_default)
{
    const robo_css_value_t *value =
        robo_css_declaration_value(declared->declaration);
    robo_css_wide_keyword_t keyword = robo_css_wide_keyword_of(value);
    const robo_css_inherited_property_t *from_parent;

    if (keyword == ROBO_CSS_WIDE_KEYWORD_REVERT) {
        /* Recognised, and deliberately not obeyed: see the header. The keyword
         * is carried as the value so nothing is lost, and the entry is counted
         * and marked deferred so nothing reads it as an answer. */
        if (!inherit_append(out, declared->property, name,
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
            inherit_append(out, declared->property, name,
                           ROBO_CSS_INHERITED_SOURCE_PARENT, keyword,
                           &from_parent->value)) {
            inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_PARENT);
            return 1;
        }
        /* `inherit` with no parent value is the parent's *computed* value, and
         * an undeclared non-inherited property's computed value is its initial
         * one — so the default is the right answer, not a failure. */
        if (!inherit_append(out, declared->property, name,
                            ROBO_CSS_INHERITED_SOURCE_DEFAULT, keyword,
                            inherit_default_value(context, name))) {
            return 0;
        }
        inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_DEFAULT);
        return 1;
    }

    if (keyword == ROBO_CSS_WIDE_KEYWORD_INITIAL ||
        (keyword == ROBO_CSS_WIDE_KEYWORD_UNSET && !inherited_by_default)) {
        if (!inherit_append(out, declared->property, name,
                            ROBO_CSS_INHERITED_SOURCE_DEFAULT, keyword,
                            inherit_default_value(context, name))) {
            return 0;
        }
        inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_DEFAULT);
        return 1;
    }

    if (!inherit_append(out, declared->property, name,
                        ROBO_CSS_INHERITED_SOURCE_DECLARED,
                        ROBO_CSS_WIDE_KEYWORD_NONE, value)) {
        return 0;
    }
    inherit_tally(out, ROBO_CSS_INHERITED_SOURCE_DECLARED);
    return 1;
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
