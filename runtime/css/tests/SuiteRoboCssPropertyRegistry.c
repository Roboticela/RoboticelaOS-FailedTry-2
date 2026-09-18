/*
 * RoboCSS property registry (Phase 9.7) — css.registry.*
 *
 * The registry is the phase's one piece of *data* rather than machinery, so this
 * suite is mostly a table check — and a table check is only worth writing if it
 * asserts the rows rather than the shape. "Every property has a classification"
 * would pass for a registry that classified all twenty-four properties the wrong
 * way round; what is asserted here is that `color` inherits and `width` does not,
 * by name, and that each property's initial value parses to the value the row
 * spells.
 *
 * The other half is the registration contract, because the registry is the first
 * RoboCSS object that can be built *by a caller* and then handed to a resolution:
 * a name it accepts and a table refuses, or an initial value that is stored
 * unparsed, would both surface much later as a mystery rather than here.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void test_css_registry_lifecycle(void)
{
    robo_css_property_registry_t *registry = NULL;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create(&registry, NULL), ROBO_OK);
    ROBO_CHECK(registry != NULL);
    /* An empty registry classifies nothing, and saying so is not an error. */
    ROBO_CHECK_EQ_INT(robo_css_property_registry_count(registry), 0u);
    ROBO_CHECK(robo_css_property_registry_find(registry, "color") == NULL);
    ROBO_CHECK(robo_css_property_registry_find(NULL, "color") == NULL);
    ROBO_CHECK(robo_css_property_registry_find(registry, NULL) == NULL);
    ROBO_CHECK(robo_css_property_registry_at(registry, 0u) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_count(NULL), 0u);

    /* Destroying twice, and destroying nothing, are both no-ops. */
    robo_css_property_registry_destroy(registry);
    robo_css_property_registry_destroy(NULL);
}

static void test_css_registry_standard_set(void)
{
    robo_css_property_registry_t *registry = NULL;
    size_t index;
    size_t inherited = 0u;
    size_t non_inherited = 0u;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);
    ROBO_CHECK(registry != NULL);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Every row is classified one way or the other, with a usable name and a
     * name/definition agreement no caller has to trust. */
    for (index = 0u; index < robo_css_property_registry_count(registry); index++) {
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_at(registry, index);

        ROBO_CHECK(definition != NULL);
        if (!definition) {
            continue;
        }
        ROBO_CHECK(definition->name != NULL);
        ROBO_CHECK(definition->name_length > 0u);
        ROBO_CHECK_EQ_INT((int)strlen(definition->name), (int)definition->name_length);
        ROBO_CHECK(robo_css_property_inheritance_is_known(definition->inheritance));
        ROBO_CHECK(definition->inheritance != ROBO_CSS_PROPERTY_INHERITANCE_UNKNOWN);
        /* The definition carries the classification as an accessor too, and the
         * two cannot disagree. */
        ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition),
                          definition->inheritance == ROBO_CSS_PROPERTY_INHERITANCE_INHERITED);
        /* Every row names an initial value, and the text parses — asserted here
         * so a row added without one is caught rather than silently defaulting. */
        ROBO_CHECK(definition->initial_text != NULL);
        if (definition->initial_text) {
            ROBO_CHECK_EQ_INT((int)strlen(definition->initial_text),
                              (int)definition->initial_text_length);
            ROBO_CHECK(robo_css_property_definition_initial_value(definition) != NULL);
            ROBO_CHECK(!robo_css_value_is_empty(
                robo_css_property_definition_initial_value(definition)));
        }
        if (robo_css_property_definition_inherited(definition)) {
            inherited++;
        } else {
            non_inherited++;
        }
    }
    /* The set is exactly the one the phase names: the nine common inherited
     * properties and the eight common non-inherited ones, plus the handful of
     * font/text relatives that answer the same classification question, and
     * `background-color` (9.9) — the second property whose value is a colour,
     * added so that the contextual `currentColor` rule is reachable at all.
     *
     * 9.11 adds fourteen: the twelve box-model longhands (`margin-*`, `padding-*`,
     * `border-*-width`), `border-width` as their third shorthand, and `box-sizing`,
     * whose value model exists so a declaration survives to the phase that has a
     * layout rather than being dropped by the phase that does not.
     *
     * A count is asserted rather than a lower bound so that a row added later is a
     * deliberate act rather than an accident. */
    ROBO_CHECK_EQ_INT((int)robo_css_property_registry_count(registry), 39);
    ROBO_CHECK_EQ_INT((int)inherited, 16);
    ROBO_CHECK_EQ_INT((int)non_inherited, 23);

    robo_css_property_registry_destroy(registry);
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

static void test_css_registry_inherited_classification(void)
{
    /* The nine the phase names, plus the font/text relatives the standard set
     * carries. Spelled out by name: a registry that classified everything the
     * same way would pass a loop over the file and fail this. */
    static const char *const inherited[] = {
        "color", "font-family", "font-size", "font-style", "font-weight",
        "line-height", "text-align", "visibility", "white-space",
        "cursor", "direction", "font-variant", "letter-spacing", "text-indent",
        "text-transform", "word-spacing"
    };
    /* The eight the phase names as the things that must NOT propagate, plus the
     * box model's fourteen (9.11) — every one of which is the *opposite* of an
     * inherited property: a `margin` that propagated would give a child its
     * parent's spacing, which is the single most visible way to be wrong. */
    static const char *const non_inherited[] = {
        "width", "height", "margin", "padding", "border", "background",
        "display", "position", "background-color",
        "margin-top", "margin-right", "margin-bottom", "margin-left",
        "padding-top", "padding-right", "padding-bottom", "padding-left",
        "border-top-width", "border-right-width", "border-bottom-width",
        "border-left-width", "border-width", "box-sizing"
    };
    robo_css_property_registry_t *registry = NULL;
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);

    for (index = 0u; index < sizeof(inherited) / sizeof(inherited[0]); index++) {
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_find(registry, inherited[index]);

        ROBO_CHECK(definition != NULL);
        if (definition) {
            ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 1);
        }
    }
    for (index = 0u;
         index < sizeof(non_inherited) / sizeof(non_inherited[0]);
         index++) {
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_find(registry, non_inherited[index]);

        ROBO_CHECK(definition != NULL);
        if (definition) {
            ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
        }
    }
    /* A property nothing registered is UNKNOWN, which is a different answer from
     * NON_INHERITED and must not be conflated with it. */
    ROBO_CHECK(robo_css_property_registry_find(registry, "totally-unknown") == NULL);

    robo_css_property_registry_destroy(registry);
}

static void test_css_registry_initial_values(void)
{
    /* Property -> the shape of its registered initial value. */
    static const struct {
        const char *name;
        const char *shape;
    } expected[] = {
        {"color", "black"},
        {"font-family", "serif"},
        {"font-size", "medium"},
        {"font-style", "normal"},
        {"font-weight", "normal"},
        {"line-height", "normal"},
        {"text-align", "start"},
        {"visibility", "visible"},
        {"white-space", "normal"},
        {"text-indent", "0"},
        {"text-transform", "none"},
        {"direction", "ltr"},
        {"cursor", "auto"},
        {"letter-spacing", "normal"},
        {"word-spacing", "normal"},
        {"font-variant", "normal"},
        {"width", "auto"},
        {"height", "auto"},
        {"display", "inline"},
        {"position", "static"},
        {"margin", "0"},
        {"padding", "0"},
        {"background", "transparent"},
        {"background-color", "transparent"},
        {"border", "medium,_,none,_,currentcolor"},
        /* 9.11's box model: each longhand's own initial, and `border-width`
         * starting where its four longhands start rather than at zero. */
        {"margin-top", "0"},
        {"margin-right", "0"},
        {"margin-bottom", "0"},
        {"margin-left", "0"},
        {"padding-top", "0"},
        {"padding-right", "0"},
        {"padding-bottom", "0"},
        {"padding-left", "0"},
        {"border-top-width", "medium"},
        {"border-right-width", "medium"},
        {"border-bottom-width", "medium"},
        {"border-left-width", "medium"},
        {"border-width", "medium"},
        {"box-sizing", "content-box"}
    };
    robo_css_property_registry_t *registry = NULL;
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_find(registry, expected[index].name);
        char shape[256];

        ROBO_CHECK(definition != NULL);
        if (!definition) {
            continue;
        }
        css_shape(robo_css_property_definition_initial_value(definition), shape,
                  sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, expected[index].shape);
    }
    /* Every row was reached, so a property renamed in the file would fail here
     * rather than silently reducing the coverage of the loop above. */
    ROBO_CHECK_EQ_INT((int)index, (int)robo_css_property_registry_count(registry));

    robo_css_property_registry_destroy(registry);
}

/* ------------------------------------------------------------------ */
/* Naming                                                              */
/* ------------------------------------------------------------------ */

static void test_css_registry_custom_property_names(void)
{
    ROBO_CHECK_EQ_INT(
        robo_css_is_custom_property_name(robo_css_slice_from_text("--x")), 1);
    ROBO_CHECK_EQ_INT(
        robo_css_is_custom_property_name(robo_css_slice_from_text("--theme-color")), 1);
    /* `--` alone is not a custom property: CSS requires at least one more byte. */
    ROBO_CHECK_EQ_INT(robo_css_is_custom_property_name(robo_css_slice_from_text("--")), 0);
    ROBO_CHECK_EQ_INT(
        robo_css_is_custom_property_name(robo_css_slice_from_text("-x")), 0);
    ROBO_CHECK_EQ_INT(
        robo_css_is_custom_property_name(robo_css_slice_from_text("color")), 0);
    ROBO_CHECK_EQ_INT(robo_css_is_custom_property_name(robo_css_slice_from_text("")), 0);
    /* A slice with no bytes is different from an empty one, and neither is a
     * custom property. */
    ROBO_CHECK_EQ_INT(robo_css_is_custom_property_name(robo_css_slice_empty()), 0);
}

static void test_css_registry_find(void)
{
    robo_css_property_registry_t *registry = NULL;
    const robo_css_property_definition_t *definition;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);
    /* Ordinary property names are ASCII case-insensitive, as CSS says. */
    ROBO_CHECK(robo_css_property_registry_find(registry, "COLOR") != NULL);
    ROBO_CHECK(robo_css_property_registry_find(registry, "CoLoR") != NULL);
    ROBO_CHECK(robo_css_property_registry_find(registry, "color") != NULL);
    definition = robo_css_property_registry_find(registry, "COLOR");
    ROBO_CHECK_STR_EQ(definition ? definition->name : "", "color");
    /* A name nothing knows, and an absent slice, are both misses. */
    ROBO_CHECK(robo_css_property_registry_find(registry, "colours") == NULL);
    ROBO_CHECK(robo_css_property_registry_find(registry, "") == NULL);
    ROBO_CHECK(robo_css_property_registry_find_slice(registry, robo_css_slice_empty()) ==
               NULL);
    /* A registered custom property is compared byte-exactly, because CSS
     * custom property names are case-sensitive where ordinary ones are not. */
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "--theme", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
                          "red", 0u, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_property_registry_find(registry, "--theme") != NULL);
    ROBO_CHECK(robo_css_property_registry_find(registry, "--THEME") == NULL);

    robo_css_property_registry_destroy(registry);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void test_css_registry_register(void)
{
    robo_css_property_registry_t *registry = NULL;
    const robo_css_property_definition_t *definition;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create(&registry, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          NULL, "color", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
                          "black", 0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* A name the property table would refuse is refused here too, so a registry
     * can always be applied to a table. */
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED, NULL,
                          0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "two words", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
                          NULL, 0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "a:b", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED, NULL,
                          0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "a.b", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED, NULL,
                          0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(registry, NULL,
                                                         ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
                                                         NULL, 0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_count(registry), 0u);

    /* UNKNOWN is never stored: a lookup that succeeded while classifying nothing
     * would be strictly worse than not finding the property. */
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "color", ROBO_CSS_PROPERTY_INHERITANCE_UNKNOWN, NULL,
                          0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "color", (robo_css_property_inheritance_t)99, NULL,
                          0u, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* An initial value that does not parse is refused rather than stored: a
     * broken row should fail where it is written. */
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "color", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
                          "red } {", 0u, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* With no initial text the property is classified and has no default — a
     * real state, and readable. */
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "color", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
                          NULL, 0u, NULL),
                      ROBO_OK);
    definition = robo_css_property_registry_find(registry, "color");
    ROBO_CHECK(definition != NULL);
    if (definition) {
        ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 1);
        ROBO_CHECK(definition->initial_text == NULL);
        ROBO_CHECK(robo_css_property_definition_initial_value(definition) == NULL);
    }

    /* Registering the same name again is reported and changes nothing: first
     * registration wins, so a classification cannot move under a reader. */
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "COLOR", ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
                          "blue", 0u, NULL),
                      ROBO_ALREADY_EXISTS);
    definition = robo_css_property_registry_find(registry, "color");
    ROBO_CHECK(definition != NULL);
    if (definition) {
        ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 1);
    }
    ROBO_CHECK_EQ_INT(robo_css_property_registry_count(registry), 1u);

    robo_css_property_registry_destroy(registry);
}

static void test_css_registry_definition_is_stable(void)
{
    robo_css_property_registry_t *registry = NULL;
    const robo_css_property_definition_t *definition;
    const robo_css_value_t *initial;
    char shape[128];
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create(&registry, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                          registry, "color", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
                          "black", 0u, NULL),
                      ROBO_OK);
    definition = robo_css_property_registry_find(registry, "color");
    initial = robo_css_property_definition_initial_value(definition);
    ROBO_CHECK(definition != NULL);
    ROBO_CHECK(initial != NULL);

    /* Register a hundred more: a borrowed definition and its initial value must
     * not move, because entries are individually allocated and addressed through
     * a pointer array. This is the property a caller relies on when it holds a
     * definition across a registration. */
    for (index = 0u; index < 100u; index++) {
        char name[32];

        (void)snprintf(name, sizeof(name), "generated-%d", (int)index);
        ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                              registry, name, ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED,
                              "auto", 0u, NULL),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT((int)robo_css_property_registry_count(registry), 101);
    css_shape(robo_css_property_definition_initial_value(definition), shape,
              sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "black");
    ROBO_CHECK(robo_css_property_definition_initial_value(definition) == initial);

    robo_css_property_registry_destroy(registry);
}

/* ------------------------------------------------------------------ */
/* Applying to a property table                                        */
/* ------------------------------------------------------------------ */

static void test_css_registry_apply_to_table(void)
{
    robo_css_property_registry_t *registry = NULL;
    robo_css_property_table_t *table = NULL;
    robo_css_property_info_t info =    ROBO_CSS_CLEARED;
    size_t registered = 0u;
    robo_css_property_id_t color_id;
    robo_css_property_id_t width_id;
    robo_css_property_id_t display_id;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_property_registry_apply_to_table(NULL, table, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_apply_to_table(registry, NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_css_property_registry_apply_to_table(registry, table,
                                                                &registered, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)registered, (int)robo_css_property_registry_count(registry));

    /* The flags a resolution reads are the registry's classifications: this is
     * what keeps the table and the registry from disagreeing. */
    color_id = robo_css_property_table_lookup(table, "color");
    width_id = robo_css_property_table_lookup(table, "width");
    display_id = robo_css_property_table_lookup(table, "display");
    ROBO_CHECK(color_id != ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK(width_id != ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK(display_id != ROBO_CSS_PROPERTY_ID_NONE);

    ROBO_CHECK_EQ_INT(robo_css_property_table_info(table, color_id, &info, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_flags_inherited(info.flags), 1);
    ROBO_CHECK_EQ_INT(robo_css_property_table_info(table, width_id, &info, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_flags_inherited(info.flags), 0);
    ROBO_CHECK_EQ_INT(robo_css_property_table_info(table, display_id, &info, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_flags_inherited(info.flags), 0);
    /* A shorthand's flag survives the trip too, so 9.9 has something to read. */
    ROBO_CHECK_EQ_INT(robo_css_property_flags_shorthand(
                          robo_css_property_table_info(table,
                                                       robo_css_property_table_lookup(table,
                                                                                      "margin"),
                                                       &info, NULL) == ROBO_OK
                              ? info.flags
                              : 0u),
                      1);

    /* Applying twice registers nothing the second time and is not an error: the
     * caller's numbering is its own. */
    registered = 99u;
    ROBO_CHECK_EQ_INT(robo_css_property_registry_apply_to_table(registry, table,
                                                                &registered, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)registered, 0);
    ROBO_CHECK_EQ_INT((int)robo_css_property_table_count(table),
                      (int)robo_css_property_registry_count(registry));

    robo_css_property_table_destroy(table);
    robo_css_property_registry_destroy(registry);
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

static void test_css_registry_classification_names(void)
{
    ROBO_CHECK_STR_EQ(robo_css_property_inheritance_name(
                          ROBO_CSS_PROPERTY_INHERITANCE_UNKNOWN),
                      "unknown");
    ROBO_CHECK_STR_EQ(robo_css_property_inheritance_name(
                          ROBO_CSS_PROPERTY_INHERITANCE_INHERITED),
                      "inherited");
    ROBO_CHECK_STR_EQ(robo_css_property_inheritance_name(
                          ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED),
                      "non-inherited");
    ROBO_CHECK_STR_EQ(robo_css_property_inheritance_name(
                          (robo_css_property_inheritance_t)77),
                      "unknown");
    ROBO_CHECK_EQ_INT(robo_css_property_inheritance_is_known(
                          ROBO_CSS_PROPERTY_INHERITANCE_UNKNOWN),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_property_inheritance_is_known(
                          ROBO_CSS_PROPERTY_INHERITANCE_NON_INHERITED),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_property_inheritance_is_known(
                          (robo_css_property_inheritance_t)77),
                      0);
    ROBO_CHECK_EQ_INT(ROBO_CSS_PROPERTY_INHERITANCE_COUNT, 3);
    /* The accessors tolerate NULL the way the rest of the model does. */
    ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(NULL), 0);
    ROBO_CHECK(robo_css_property_definition_initial_value(NULL) == NULL);
}

/* ------------------------------------------------------------------ */
/* Ownership                                                           */
/* ------------------------------------------------------------------ */

static void test_css_registry_releases_everything(void)
{
    int enabled;
    long baseline;

    css_fixture();
    enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();

    /* Names, initial texts and their parsed value trees all belong to the
     * registry, so dropping it must return the allocator to where it started. A
     * registry whose definitions leaked their initial value's children would
     * show up here and nowhere else. */
    {
        robo_css_property_registry_t *registry = NULL;

        ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                              registry, "--custom", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
                              "calc(100% - 12px)", 0u, NULL),
                          ROBO_OK);
        /* A refused registration must not leak the parts it built first. The
         * text is an unterminated block: a value the grammar cannot finish is a
         * value it cannot return, which is the case the registry refuses. */
        ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                              registry, "broken", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
                              "{", 0u, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_property_registry_register(
                              registry, "color", ROBO_CSS_PROPERTY_INHERITANCE_INHERITED,
                              "black", 0u, NULL),
                          ROBO_ALREADY_EXISTS);
        robo_css_property_registry_destroy(registry);
    }

    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(enabled);
}

void robo_test_register_css_property_registry(void);

void robo_test_register_css_property_registry(void)
{
    robo_test_register("css.registry.lifecycle", test_css_registry_lifecycle);
    robo_test_register("css.registry.standard_set", test_css_registry_standard_set);
    robo_test_register("css.registry.inherited_classification",
                       test_css_registry_inherited_classification);
    robo_test_register("css.registry.initial_values", test_css_registry_initial_values);
    robo_test_register("css.registry.custom_property_names",
                       test_css_registry_custom_property_names);
    robo_test_register("css.registry.find", test_css_registry_find);
    robo_test_register("css.registry.register", test_css_registry_register);
    robo_test_register("css.registry.definition_is_stable",
                       test_css_registry_definition_is_stable);
    robo_test_register("css.registry.apply_to_table", test_css_registry_apply_to_table);
    robo_test_register("css.registry.classification_names",
                       test_css_registry_classification_names);
    robo_test_register("css.registry.releases_everything",
                       test_css_registry_releases_everything);
}
