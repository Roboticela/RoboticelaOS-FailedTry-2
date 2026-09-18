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
 * An entry *is* a robo_css_property_definition_t — the definition struct is
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

struct robo_css_property_registry {
    robo_css_property_definition_t **entries; /* owned; each element owned too */
    size_t count;
    size_t capacity;
};

/* ------------------------------------------------------------------ */
/* Naming                                                             */
/* ------------------------------------------------------------------ */

int robo_css_is_custom_property_name(robo_css_slice_t name)
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
static int registry_entry_matches(const robo_css_property_definition_t *entry,
                                  robo_css_slice_t name)
{
    robo_css_slice_t stored =
        robo_css_slice_from_bytes((const uint8_t *)entry->name, entry->name_length);

    if (robo_css_is_custom_property_name(name) ||
        robo_css_is_custom_property_name(stored)) {
        return robo_css_slice_equals(stored, name);
    }
    return robo_css_slice_equals_ci(stored, name);
}

/**
 * A property name the property table would also accept: non-empty, bounded,
 * with no NUL, whitespace, `:` or `.`. Duplicated from RoboCSSProperty.c on
 * purpose? No — it is *the same rule*, and it is stated here rather than shared
 * because the two live in different translation units with no internal header
 * between them for it, and a name the registry accepts and the table refuses
 * would be a failure at apply time for no reason the caller could see.
 */
static int registry_name_is_usable(const char *name, size_t *out_length)
{
    size_t index;
    size_t length;

    if (!name) {
        return 0;
    }
    length = strlen(name);
    if (length == 0u || length > (size_t)ROBO_CSS_PROPERTY_NAME_MAX) {
        return 0;
    }
    for (index = 0u; index < length; index++) {
        uint8_t c = (uint8_t)name[index];

        if (c == 0u || robo_css_ascii_is_space(c) || c == (uint8_t)':' ||
            c == (uint8_t)'.') {
            return 0;
        }
    }
    *out_length = length;
    return 1;
}

static char *registry_copy_text(const char *text, size_t length)
{
    char *copy = (char *)robo_malloc(length + 1u);

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

int robo_css_property_inheritance_is_known(robo_css_property_inheritance_t inheritance)
{
    return inheritance == ROBO_CSS_PROPERTY_INHERITANCE_UNKNOWN ||
           inheritance == ROBO_CSS_PROPERTY_INHERITANCE_INHERITED ||
           inheritance == ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED;
}

const char *robo_css_property_inheritance_name(
    robo_css_property_inheritance_t inheritance)
{
    switch (inheritance) {
    case ROBO_CSS_PROPERTY_INHERITANCE_UNKNOWN:
        return "unknown";
    case ROBO_CSS_PROPERTY_INHERITANCE_INHERITED:
        return "inherited";
    case ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED:
        return "non-inherited";
    default:
        return "unknown";
    }
}

int robo_css_property_computation_is_known(robo_css_property_computation_t computation)
{
    return computation >= ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE &&
           computation < (robo_css_property_computation_t)ROBO_CSS_PROPERTY_COMPUTATION_COUNT;
}

const char *robo_css_property_computation_name(
    robo_css_property_computation_t computation)
{
    switch (computation) {
    case ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE:
        return "preserve";
    case ROBO_CSS_PROPERTY_COMPUTATION_COLOR:
        return "colour";
    case ROBO_CSS_PROPERTY_COMPUTATION_LENGTH:
        return "length";
    case ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD:
        return "keyword";
    case ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY:
        return "font-family";
    case ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT:
        return "font-weight";
    case ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT:
        return "line-height";
    case ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET:
        return "edge offset";
    case ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET:
        return "edge inset";
    case ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH:
        return "line width";
    case ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY:
        return "display";
    case ROBO_CSS_PROPERTY_COMPUTATION_POSITION:
        return "position";
    case ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX:
        return "z-index";
    case ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT:
        return "background-repeat";
    case ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT:
        return "background-attachment";
    case ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE:
        return "background-image";
    case ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION:
        return "background-position";
    case ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE:
        return "background-size";
    case ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE:
        return "border-style";
    default:
        return "unknown";
    }
}

int robo_css_property_definition_inherited(
    const robo_css_property_definition_t *definition)
{
    return definition &&
           definition->inheritance == ROBO_CSS_PROPERTY_INHERITANCE_INHERITED;
}

const robo_css_value_t *robo_css_property_definition_initial_value(
    const robo_css_property_definition_t *definition)
{
    if (!definition || definition->initial_text == NULL) {
        return NULL;
    }
    return &definition->initial_value;
}

/* ------------------------------------------------------------------ */
/* Registry lifecycle                                                 */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_property_registry_create(robo_css_property_registry_t **out_registry,
                                                robo_css_diag_t *diag)
{
    robo_css_property_registry_t *registry;

    if (!out_registry) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_INVALID_ARGUMENT,
                             "output is NULL");
    }
    *out_registry = NULL;
    registry = (robo_css_property_registry_t *)robo_calloc(
        1u, sizeof(robo_css_property_registry_t));
    if (!registry) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_OUT_OF_MEMORY,
                             "could not allocate the registry");
    }
    *out_registry = registry;
    return ROBO_OK;
}

void robo_css_property_registry_destroy(robo_css_property_registry_t *registry)
{
    size_t index;

    if (!registry) {
        return;
    }
    for (index = 0u; index < registry->count; index++) {
        robo_css_property_definition_t *entry = registry->entries[index];

        if (!entry) {
            continue;
        }
        /* The value before the text it borrows, so nothing reads freed bytes
         * during disposal even though disposal does not read them. */
        robo_css_value_dispose(&entry->initial_value);
        robo_free(entry->initial_text);
        robo_free(entry->name);
        robo_free(entry);
    }
    robo_free(registry->entries);
    robo_free(registry);
}

const robo_css_property_definition_t *robo_css_property_registry_at(
    const robo_css_property_registry_t *registry,
    size_t index)
{
    if (!registry || index >= registry->count) {
        return NULL;
    }
    return registry->entries[index];
}

const robo_css_property_definition_t *robo_css_property_registry_find_slice(
    const robo_css_property_registry_t *registry,
    robo_css_slice_t name)
{
    size_t index;

    if (!registry || robo_css_slice_is_absent(name)) {
        return NULL;
    }
    for (index = 0u; index < registry->count; index++) {
        if (registry_entry_matches(registry->entries[index], name)) {
            return robo_css_property_registry_at(registry, index);
        }
    }
    return NULL;
}

const robo_css_property_definition_t *robo_css_property_registry_find(
    const robo_css_property_registry_t *registry,
    const char *name)
{
    if (!name) {
        return NULL;
    }
    return robo_css_property_registry_find_slice(registry,
                                                 robo_css_slice_from_text(name));
}

size_t robo_css_property_registry_count(const robo_css_property_registry_t *registry)
{
    return registry ? registry->count : 0u;
}

const robo_css_property_definition_t *robo_css_property_registry_find_property(
    const robo_css_property_registry_t *registry,
    const robo_css_property_table_t *table,
    robo_css_property_id_t property)
{
    size_t index;

    if (!registry || !table || property == ROBO_CSS_PROPERTY_ID_NONE) {
        return NULL;
    }
    for (index = 0u; index < registry->count; index++) {
        const robo_css_property_definition_t *entry = registry->entries[index];

        if (!entry) {
            continue;
        }
        if (robo_css_property_table_lookup(table, entry->name) == property) {
            return entry;
        }
    }
    return NULL;
}

robo_result_t robo_css_property_registry_register(robo_css_property_registry_t *registry,
                                                  const char *name,
                                                  robo_css_property_inheritance_t inheritance,
                                                  const char *initial_text,
                                                  uint32_t flags,
                                                  robo_css_diag_t *diag)
{
    return robo_css_property_registry_register_typed(
        registry, name, inheritance, ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,
        initial_text, flags, diag);
}

robo_result_t robo_css_property_registry_register_typed(
    robo_css_property_registry_t *registry,
    const char *name,
    robo_css_property_inheritance_t inheritance,
    robo_css_property_computation_t computation,
    const char *initial_text,
    uint32_t flags,
    robo_css_diag_t *diag)
{
    robo_css_property_definition_t *entry;
    size_t name_length = 0u;

    if (!registry) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_INVALID_ARGUMENT,
                             "registry is NULL");
    }
    if (!robo_css_property_computation_is_known(computation)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_INVALID_ARGUMENT,
                             "not a value-computation classification");
    }
    if (!registry_name_is_usable(name, &name_length)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_INVALID_ARGUMENT,
                             "not a usable property name");
    }
    if (inheritance != ROBO_CSS_PROPERTY_INHERITANCE_INHERITED &&
        inheritance != ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED) {
        /* UNKNOWN is never *stored*: a definition that says nothing would be a
         * way for a lookup to succeed while classifying nothing, which is
         * strictly worse than not finding the property at all. */
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_INVALID_ARGUMENT,
                             "a definition must classify the property");
    }
    if (robo_css_property_registry_find(registry, name) != NULL) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_ALREADY_EXISTS,
                             "property is already registered");
    }

    entry = (robo_css_property_definition_t *)robo_calloc(
        1u, sizeof(robo_css_property_definition_t));
    if (!entry) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_OUT_OF_MEMORY,
                             "could not allocate the definition");
    }
    robo_css_value_init(&entry->initial_value);
    entry->name = registry_copy_text(name, name_length);
    if (!entry->name) {
        robo_free(entry);
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_OUT_OF_MEMORY,
                             "could not copy the property name");
    }
    entry->name_length = name_length;
    entry->inheritance = inheritance;
    entry->computation = computation;
    entry->flags = flags;

    if (initial_text != NULL) {
        robo_css_source_t source = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;
        size_t text_length = strlen(initial_text);

        entry->initial_text = registry_copy_text(initial_text, text_length);
        if (!entry->initial_text) {
            robo_free(entry->name);
            robo_free(entry);
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_OUT_OF_MEMORY,
                                 "could not copy the initial value text");
        }
        entry->initial_text_length = text_length;
        /* Parsed once, here. An initial value that does not parse is a defect
         * in the registry, and it (registry) is where it should be caught. */
        (void)robo_css_source_init(&source);
        if (robo_css_source_set_memory(&source, (const uint8_t *)entry->initial_text,
                                       entry->initial_text_length, NULL) != ROBO_OK ||
            robo_css_grammar_parse_value(&entry->initial_value, &source,
                                         ROBO_CSS_ORIGIN_NONE, &error) != ROBO_OK ||
            robo_css_error_is_error(&error)) {
            robo_css_source_dispose(&source);
            robo_css_value_dispose(&entry->initial_value);
            robo_free(entry->initial_text);
            robo_free(entry->name);
            robo_free(entry);
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_INVALID_ARGUMENT,
                                 "the initial value text does not parse");
        }
        robo_css_source_dispose(&source);
    }

    if (!robo_css_grow((void **)&registry->entries, &registry->capacity, registry->count,
                       sizeof(robo_css_property_definition_t *))) {
        robo_css_value_dispose(&entry->initial_value);
        robo_free(entry->initial_text);
        robo_free(entry->name);
        robo_free(entry);
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_OUT_OF_MEMORY,
                             "could not grow the registry");
    }
    registry->entries[registry->count] = entry;
    registry->count++;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The standard set                                                   */
/* ------------------------------------------------------------------ */

/** One row of the standard set: name, classification, value shape, initial, flags. */
typedef struct registry_standard_entry {
    const char *name;
    robo_css_property_inheritance_t inheritance;
    robo_css_property_computation_t computation;
    const char *initial_text;
    uint32_t flags;
} registry_standard_entry_t;

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
static const registry_standard_entry_t registry_standard_entries[] = {
    /* --- inherited --- */
    { "color",         ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_COLOR,      "black",      0 },
    { "cursor",        ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,    "auto",       0 },
    { "direction",     ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,    "ltr",        0 },
    { "font-family",   ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,"serif",      0 },
    { "font-size",     ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,     "medium",     0 },
    { "font-style",    ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,    "normal",     0 },
    { "font-variant",  ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,    "normal",     0 },
    { "font-weight",   ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,"normal",     0 },
    { "letter-spacing",ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,     "normal",     0 },
    { "line-height",   ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT,"normal",     0 },
    { "text-align",    ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,    "start",      0 },
    { "text-indent",   ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,     "0",          0 },
    { "text-transform",ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,    "none",       0 },
    { "visibility",    ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,    "visible",    0 },
    { "white-space",   ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,    "normal",     0 },
    { "word-spacing",  ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,     "normal",     0 },

    /* --- non-inherited --- */
    { "background",    ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "transparent",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },
    /* 9.9 added this row, and the reason is not backgrounds: it is the only
     * second property whose value *is* a colour, and without one the rule that
     * resolves `currentColor` against the element's own `color` — as opposed to
     * the parent's, which is what it means on `color` itself — would be
     * unreachable and therefore unprovable. */
    { "background-color", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_COLOR,      "transparent", 0 },

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
    { "background-image",      ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,      "none",    0 },
    { "background-position",   ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,   "0% 0%",   0 },
    { "background-size",       ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,       "auto",    0 },
    { "background-repeat",     ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,     "repeat",  0 },
    { "background-attachment", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT, "scroll",  0 },
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
    { "border-bottom-width", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "medium",     0 },
    { "border-left-width", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "medium",     0 },
    { "border-right-width", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "medium",     0 },
    { "border-top-width", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "medium",     0 },

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
    { "border",          ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "medium none currentcolor",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },
    { "border-bottom",   ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "medium none currentcolor",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },
    { "border-bottom-color", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_COLOR,      "currentcolor", 0 },
    { "border-bottom-style", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE, "none",       0 },
    { "border-color",    ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "currentcolor",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },
    { "border-left",     ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "medium none currentcolor",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },
    { "border-left-color", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_COLOR,      "currentcolor", 0 },
    { "border-left-style", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE, "none",       0 },
    { "border-right",    ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "medium none currentcolor",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },
    { "border-right-color", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_COLOR,      "currentcolor", 0 },
    { "border-right-style", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE, "none",       0 },
    { "border-style",    ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "none",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },
    { "border-top",      ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "medium none currentcolor",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },
    { "border-top-color", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_COLOR,      "currentcolor", 0 },
    { "border-top-style", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE, "none",       0 },
    { "border-width",    ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "medium",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },

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
    { "bottom",        ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "auto",      0 },
    { "box-sizing",    ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,    "content-box", 0 },
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
    { "display",       ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY,    "inline",     0 },
    { "height",        ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,     "auto",       0 },
    { "left",          ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "auto",      0 },
    { "margin",        ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "0",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },
    { "margin-bottom", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "0",         0 },
    { "margin-left",   ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "0",         0 },
    { "margin-right",  ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "0",         0 },
    { "margin-top",    ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "0",         0 },
    { "padding",       ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,   "0",
      ROBO_CSS_PROPERTY_FLAG_SHORTHAND },
    { "padding-bottom", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET,  "0",         0 },
    { "padding-left",  ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET,  "0",         0 },
    { "padding-right", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET,  "0",         0 },
    { "padding-top",   ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET,  "0",         0 },
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
    { "position",      ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_POSITION,   "static",     0 },
    { "right",         ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "auto",      0 },
    { "top",           ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "auto",      0 },
    { "width",         ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,     "auto",       0 },
    { "z-index",       ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
      ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX,    "auto",       0 }
};

robo_result_t robo_css_property_registry_create_standard(
    robo_css_property_registry_t **out_registry,
    robo_css_diag_t *diag)
{
    robo_css_property_registry_t *registry = NULL;
    size_t index;
    robo_result_t result;

    if (!out_registry) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_INVALID_ARGUMENT,
                             "output is NULL");
    }
    *out_registry = NULL;
    result = robo_css_property_registry_create(&registry, diag);
    if (result != ROBO_OK) {
        return result;
    }
    for (index = 0u;
         index < sizeof(registry_standard_entries) / sizeof(registry_standard_entries[0]);
         index++) {
        const registry_standard_entry_t *entry = &registry_standard_entries[index];

        result = robo_css_property_registry_register_typed(
            registry, entry->name, entry->inheritance, entry->computation,
            entry->initial_text, entry->flags, diag);
        if (result != ROBO_OK) {
            /* Fully built or not at all: a half-populated standard set would
             * classify some properties and silently not others. */
            robo_css_property_registry_destroy(registry);
            return result;
        }
    }
    *out_registry = registry;
    return ROBO_OK;
}

robo_result_t robo_css_property_registry_apply_to_table(
    const robo_css_property_registry_t *registry,
    robo_css_property_table_t *table,
    size_t *out_registered,
    robo_css_diag_t *diag)
{
    size_t index;
    size_t registered = 0u;

    if (out_registered) {
        *out_registered = 0u;
    }
    if (!registry || !table) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, ROBO_INVALID_ARGUMENT,
                             "registry or table is NULL");
    }
    for (index = 0u; index < registry->count; index++) {
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_at(registry, index);
        robo_css_property_id_t id = ROBO_CSS_PROPERTY_ID_NONE;
        uint32_t flags = definition->flags;
        robo_result_t result;

        if (!definition) {
            continue;
        }

        if (robo_css_property_definition_inherited(definition)) {
            flags |= ROBO_CSS_PROPERTY_FLAG_INHERITED;
        }
        result = robo_css_property_table_register(table, definition->name, flags, &id, NULL);
        if (result == ROBO_ALREADY_EXISTS) {
            /* The caller numbered this property itself. Its flags stay its own
             * — first registration wins, the same rule the table already has. */
            continue;
        }
        if (result != ROBO_OK) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY_REGISTRY, result,
                                 "a property could not be registered in the table");
        }
        registered++;
    }
    if (out_registered) {
        *out_registered = registered;
    }
    return ROBO_OK;
}
