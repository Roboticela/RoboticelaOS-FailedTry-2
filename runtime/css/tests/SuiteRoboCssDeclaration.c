/*
 * RoboCSS Phase 9.1 declaration suite:
 * property identity, the declaration record, declaration lists and the
 * inline-style parse boundary.
 */

#include "RoboCssTestSupport.h"

static void test_css_property_table_lifecycle(void)
{
    robo_css_property_table_t *table = NULL;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);
    ROBO_CHECK(table != NULL);
    ROBO_CHECK_EQ_INT(robo_css_property_table_count(table), 0u);
    /* A fresh table knows no property: that is the honest state of a build with
     * no property families implemented. */
    ROBO_CHECK_EQ_INT(robo_css_property_table_lookup(table, "color"),
                      ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK(!robo_css_property_table_is_registered(table, 1u));

    robo_css_property_table_destroy(table);
    robo_css_property_table_destroy(NULL);
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_table_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_property_table_lookup(NULL, "color"),
                      ROBO_CSS_PROPERTY_ID_NONE);
    robo_css_property_table_reset(NULL);
}

static void test_css_property_register_and_lookup(void)
{
    robo_css_property_table_t *table = NULL;
    robo_css_property_id_t color = ROBO_CSS_PROPERTY_ID_NONE;
    robo_css_property_id_t again = ROBO_CSS_PROPERTY_ID_NONE;
    robo_css_property_id_t padding = ROBO_CSS_PROPERTY_ID_NONE;
    robo_css_property_info_t info;
    robo_css_diag_t diag;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_property_table_register(
                          table, "color", ROBO_CSS_PROPERTY_FLAG_INHERITED, &color,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK(color != ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_css_property_table_count(table), 1u);

    /* Lookup is ASCII case-insensitive and works from a slice as well as a
     * string: a declaration's name is a slice. */
    ROBO_CHECK_EQ_INT(robo_css_property_table_lookup(table, "color"), color);
    ROBO_CHECK_EQ_INT(robo_css_property_table_lookup(table, "COLOR"), color);
    ROBO_CHECK_EQ_INT(robo_css_property_table_lookup(table, "CoLoR"), color);
    ROBO_CHECK_EQ_INT(robo_css_property_table_lookup_slice(
                          table, robo_css_slice_from_text("Color")),
                      color);
    ROBO_CHECK_EQ_INT(robo_css_property_table_lookup(table, "colour"),
                      ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_css_property_table_lookup(table, ""),
                      ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_css_property_table_lookup_slice(table, robo_css_slice_empty()),
                      ROBO_CSS_PROPERTY_ID_NONE);

    /* Registering a name that is already present answers ALREADY_EXISTS with the
     * existing id, and the stored flags are the first registration's. */
    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "COLOR",
                                                       ROBO_CSS_PROPERTY_FLAG_SHORTHAND,
                                                       &again, &diag),
                      ROBO_ALREADY_EXISTS);
    ROBO_CHECK_EQ_INT(again, color);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_PROPERTY);
    ROBO_CHECK_EQ_INT(robo_css_property_table_count(table), 1u);
    ROBO_CHECK_EQ_INT(robo_css_property_table_info(table, color, &info, NULL), ROBO_OK);
    ROBO_CHECK(robo_css_property_flags_inherited(info.flags));
    ROBO_CHECK(!robo_css_property_flags_shorthand(info.flags));

    /* The name comes back as written, not lowercased: normalising case is the
     * tokenizer's job, and a caller reading back what it registered must see
     * what it registered. */
    ROBO_CHECK_STR_EQ(info.name, "color");
    ROBO_CHECK_EQ_INT(info.name_length, 5u);
    ROBO_CHECK(robo_css_property_info_is_set(&info));
    ROBO_CHECK_EQ_INT(info.id, color);

    ROBO_CHECK_EQ_INT(robo_css_property_table_register(
                          table, "padding", ROBO_CSS_PROPERTY_FLAG_SHORTHAND, &padding,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK(padding != color);
    ROBO_CHECK_EQ_INT(robo_css_property_table_count(table), 2u);
    ROBO_CHECK_EQ_INT(robo_css_property_table_at(table, 0u, &info, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(info.id, color);
    ROBO_CHECK_EQ_INT(robo_css_property_table_at(table, 1u, &info, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(info.id, padding);
    ROBO_CHECK_EQ_INT(robo_css_property_table_at(table, 2u, &info, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_css_property_table_info(table, (robo_css_property_id_t)99,
                                                   &info, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_css_property_table_info(table, ROBO_CSS_PROPERTY_ID_NONE,
                                                   &info, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_css_property_table_info(table, color, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_table_at(table, 0u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_property_table_destroy(table);
}

static void test_css_property_rejects_bad_names(void)
{
    robo_css_property_table_t *table = NULL;
    robo_css_property_id_t id = ROBO_CSS_PROPERTY_ID_NONE;
    char too_long[ROBO_CSS_PROPERTY_NAME_MAX + 8];
    size_t i;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "", 0u, &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, NULL, 0u, &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* A name that could not be a property name is refused rather than stored. */
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "text-align:center", 0u,
                                                       &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "a b", 0u, &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "border.radius", 0u,
                                                       &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_table_count(table), 0u);
    ROBO_CHECK_EQ_INT(id, ROBO_CSS_PROPERTY_ID_NONE);

    for (i = 0u; i + 1u < sizeof(too_long); i++) {
        too_long[i] = 'x';
    }
    too_long[sizeof(too_long) - 1u] = '\0';
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, too_long, 0u, &id, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A vendor prefix is a name like any other, and a custom property is not
     * special-cased in this phase — it registers as a plain name. */
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "-webkit-line-clamp", 0u,
                                                       &id, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "--brand", 0u, &id, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_count(table), 2u);

    ROBO_CHECK_EQ_INT(robo_css_property_table_register(NULL, "color", 0u, &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "color", 0u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_property_table_destroy(table);
}

static void test_css_property_ids_never_reissued(void)
{
    robo_css_property_table_t *table = NULL;
    robo_css_property_id_t before = ROBO_CSS_PROPERTY_ID_NONE;
    robo_css_property_id_t after = ROBO_CSS_PROPERTY_ID_NONE;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "color", 0u, &before,
                                                       NULL),
                      ROBO_OK);
    robo_css_property_table_reset(table);
    ROBO_CHECK_EQ_INT(robo_css_property_table_count(table), 0u);
    /* An id issued before a reset must not name whatever is registered next. */
    ROBO_CHECK(!robo_css_property_table_is_registered(table, before));
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "padding", 0u, &after,
                                                       NULL),
                      ROBO_OK);
    ROBO_CHECK(after != before);

    robo_css_property_table_destroy(table);
}

static void test_css_declaration_model(void)
{
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
    robo_css_diag_t diag;

    css_fixture();
    robo_css_declaration_init(&declaration);
    ROBO_CHECK_EQ_INT(declaration.state, ROBO_CSS_DECLARATION_STATE_NONE);
    ROBO_CHECK(!robo_css_declaration_is_usable(&declaration));
    ROBO_CHECK(!robo_css_declaration_has_value(&declaration));
    ROBO_CHECK(robo_css_slice_is_empty(robo_css_declaration_name(&declaration)));

    ROBO_CHECK_EQ_INT(robo_css_declaration_make(&declaration,
                                                robo_css_slice_from_text("color"),
                                                ROBO_CSS_DECLARATION_STATE_PARSED, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_declaration_is_usable(&declaration));
    ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_declaration_name(&declaration),
                                           "color"));
    ROBO_CHECK_EQ_INT(declaration.property, ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK_EQ_INT(declaration.important, 0);

    /* A declaration cannot be made in the NONE state — that state means "no
     * declaration" and is what clearing produces, not something to build. */
    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_declaration_make(&declaration,
                                                robo_css_slice_from_text("x"),
                                                ROBO_CSS_DECLARATION_STATE_NONE, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_DECLARATION);
    ROBO_CHECK(robo_css_declaration_is_usable(&declaration));

    /* A PARSED declaration needs a name; an INVALID one is exactly what an
     * empty name looks like, so it is allowed there. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_make(&declaration, robo_css_slice_empty(),
                                                ROBO_CSS_DECLARATION_STATE_PARSED, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_declaration_make(&declaration, robo_css_slice_empty(),
                                                ROBO_CSS_DECLARATION_STATE_INVALID, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(declaration.state, ROBO_CSS_DECLARATION_STATE_INVALID);
    ROBO_CHECK(!robo_css_declaration_is_usable(&declaration));

    ROBO_CHECK_EQ_INT(robo_css_declaration_make(NULL, robo_css_slice_from_text("a"),
                                                ROBO_CSS_DECLARATION_STATE_PARSED, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_css_declaration_dispose(&declaration);
    robo_css_declaration_dispose(NULL);
    ROBO_CHECK_EQ_INT(robo_css_declaration_state_is_known(declaration.state), 1);
    ROBO_CHECK_STR_EQ(robo_css_declaration_state_name(
                          ROBO_CSS_DECLARATION_STATE_INVALID),
                      "invalid");
    ROBO_CHECK(!robo_css_declaration_state_is_known((robo_css_declaration_state_t)9));
    ROBO_CHECK_STR_EQ(robo_css_declaration_state_name((robo_css_declaration_state_t)9),
                      "unknown");
    ROBO_CHECK(robo_css_declaration_value(NULL) == NULL);
}

static void test_css_declaration_value_and_importance(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
    robo_css_value_t value = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "12px !important");
    robo_css_declaration_make(&declaration, robo_css_slice_from_text("padding-left"),
                              ROBO_CSS_DECLARATION_STATE_PARSED, NULL);

    ROBO_CHECK_EQ_INT(robo_css_declaration_set_important(&declaration, 1, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(declaration.important, 1);
    ROBO_CHECK_EQ_INT(robo_css_declaration_set_important(&declaration, 0, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(declaration.important, 0);
    /* Importance is normalised to 0/1 rather than stored as the caller's int. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_set_important(&declaration, 42, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(declaration.important, 1);

    ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_DIMENSION, &source,
                                          robo_css_range_span(0u, 4u), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_value_make_token(&value, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&declaration, &value, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_value_is_empty(&value));
    ROBO_CHECK(robo_css_declaration_has_value(&declaration));
    ROBO_CHECK(robo_css_slice_equals_ascii(
                   robo_css_value_token_text(robo_css_declaration_value(&declaration)),
                   "12px"));

    /* Setting a second value replaces the first and disposes it: one owner. */
    {
        robo_css_value_t function = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_value_make_function(&function,
                                                       robo_css_slice_from_text("calc"),
                                                       robo_css_range_at(0u), NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&declaration, &function, NULL),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_css_declaration_value(&declaration)->kind,
                      ROBO_CSS_VALUE_KIND_FUNCTION);

    ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&declaration, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_declaration_set_important(NULL, 1, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A declaration that has not been made refuses a value. */
    {
        robo_css_declaration_t empty = ROBO_CSS_CLEARED;
        robo_css_value_t list = ROBO_CSS_CLEARED;

        robo_css_declaration_init(&empty);
        ROBO_CHECK_EQ_INT(robo_css_value_make_list(&list, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&empty, &list, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(list.kind, ROBO_CSS_VALUE_KIND_LIST);
        robo_css_value_dispose(&list);
    }

    robo_css_declaration_dispose(&declaration);
    robo_css_source_dispose(&source);
}

static void test_css_declaration_resolve_property(void)
{
    robo_css_property_table_t *table = NULL;
    robo_css_property_id_t color = ROBO_CSS_PROPERTY_ID_NONE;
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "color", 0u, &color,
                                                       NULL),
                      ROBO_OK);

    robo_css_declaration_make(&declaration, robo_css_slice_from_text("Color"),
                              ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
    ROBO_CHECK_EQ_INT(robo_css_declaration_resolve_property(&declaration, table),
                      color);
    ROBO_CHECK_EQ_INT(declaration.property, color);
    /* The name slice is untouched by resolution: the declaration still carries
     * what the author wrote. */
    ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_declaration_name(&declaration),
                                           "Color"));

    /* An unknown property is a result, not a failure: NONE, no error. */
    robo_css_declaration_make(&declaration, robo_css_slice_from_text("unknown-prop"),
                              ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
    ROBO_CHECK_EQ_INT(robo_css_declaration_resolve_property(&declaration, table),
                      ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK_EQ_INT(declaration.property, ROBO_CSS_PROPERTY_ID_NONE);

    ROBO_CHECK_EQ_INT(robo_css_declaration_resolve_property(&declaration, NULL),
                      ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_css_declaration_resolve_property(NULL, table),
                      ROBO_CSS_PROPERTY_ID_NONE);

    robo_css_declaration_dispose(&declaration);
    robo_css_property_table_destroy(table);
}

static void test_css_declaration_ranges(void)
{
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;

    css_fixture();
    robo_css_declaration_make(&declaration, robo_css_slice_from_text("color"),
                              ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
    ROBO_CHECK_EQ_INT(robo_css_declaration_set_ranges(&declaration,
                                                      robo_css_range_span(3u, 12u),
                                                      robo_css_range_span(3u, 5u),
                                                      robo_css_range_span(10u, 3u),
                                                      NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_range_length(declaration.range), 12u);
    ROBO_CHECK_EQ_INT(declaration.name_range.begin, 3u);
    ROBO_CHECK_EQ_INT(declaration.name_range.end, 8u);
    ROBO_CHECK_EQ_INT(declaration.value_range.begin, 10u);
    ROBO_CHECK_EQ_INT(robo_css_declaration_set_ranges(NULL, robo_css_range_at(0u),
                                                      robo_css_range_at(0u),
                                                      robo_css_range_at(0u), NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_css_declaration_dispose(&declaration);
}

static void test_css_declaration_list(void)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
    robo_css_property_table_t *table = NULL;
    robo_css_property_id_t color = ROBO_CSS_PROPERTY_ID_NONE;
    robo_css_property_id_t padding = ROBO_CSS_PROPERTY_ID_NONE;
    size_t index = 99u;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "color", 0u, &color,
                                                       NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "padding", 0u, &padding,
                                                       NULL),
                      ROBO_OK);

    robo_css_declaration_list_init(&list);
    ROBO_CHECK(robo_css_declaration_list_is_empty(&list));
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 0u);
    ROBO_CHECK(robo_css_declaration_list_at(&list, 0u) == NULL);

    /* Source order is preserved, duplicates included: dropping a duplicate here
     * would be implementing the cascade's last-one-wins rule. */
    {
        static const char *names[] = {"color", "padding", "color", "color"};
        int i;

        for (i = 0; i < 4; i++) {
            robo_css_declaration_make(&declaration, robo_css_slice_from_text(names[i]),
                                      ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
            (void)robo_css_declaration_resolve_property(&declaration, table);
            ROBO_CHECK_EQ_INT(robo_css_declaration_list_append(&list, &declaration,
                                                               NULL),
                              ROBO_OK);
            /* The appended declaration was moved: the local is empty again. */
            ROBO_CHECK_EQ_INT(declaration.state, ROBO_CSS_DECLARATION_STATE_NONE);
        }
    }
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 4u);
    ROBO_CHECK(!robo_css_declaration_list_is_empty(&list));
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count_of(&list, color), 3u);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count_of(&list, padding), 1u);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count_of(&list,
                                                         ROBO_CSS_PROPERTY_ID_NONE), 0u);
    ROBO_CHECK(robo_css_declaration_list_find_first(&list, color, &index));
    ROBO_CHECK_EQ_INT(index, 0u);
    ROBO_CHECK(robo_css_declaration_list_find_last(&list, color, &index));
    ROBO_CHECK_EQ_INT(index, 3u);
    ROBO_CHECK(robo_css_declaration_list_find_first(&list, padding, &index));
    ROBO_CHECK_EQ_INT(index, 1u);
    /* Both ends of a repeated property are visible; which one wins is 9.5's
     * decision and this module does not make it. */
    ROBO_CHECK(!robo_css_declaration_list_find_first(&list,
                                                     ROBO_CSS_PROPERTY_ID_NONE, &index));
    ROBO_CHECK(!robo_css_declaration_list_find_last(&list, (robo_css_property_id_t)99,
                                                    &index));
    ROBO_CHECK(!robo_css_declaration_list_find_first(NULL, color, &index));

    /* The borrowed accessors agree with the order. */
    ROBO_CHECK(robo_css_slice_equals_ascii(
                   robo_css_declaration_name(robo_css_declaration_list_at(&list, 1u)),
                   "padding"));
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 1u)->property, padding);
    ROBO_CHECK(robo_css_declaration_list_at(&list, 4u) == NULL);

    robo_css_declaration_list_clear(&list);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 0u);
    /* Clearing keeps the allocation, so the list is still usable. */
    robo_css_declaration_make(&declaration, robo_css_slice_from_text("color"),
                              ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_append(&list, &declaration, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);

    robo_css_declaration_list_dispose(&list);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 0u);
    robo_css_declaration_list_dispose(&list);
    robo_css_declaration_list_dispose(NULL);
    robo_css_declaration_list_clear(NULL);
    robo_css_declaration_list_init(NULL);
    ROBO_CHECK(robo_css_declaration_list_is_empty(NULL));
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_append(NULL, &declaration, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_append(&list, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A declaration that was never made is refused by the list. */
    {
        robo_css_declaration_t unmade = ROBO_CSS_CLEARED;

        robo_css_declaration_init(&unmade);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_append(&list, &unmade, NULL),
                          ROBO_INVALID_ARGUMENT);
    }

    robo_css_property_table_destroy(table);
}

static void test_css_declaration_list_lifetime_balances(void)
{
    int we_enabled;
    size_t baseline;

    css_fixture();
    we_enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();
    {
        int i;

        for (i = 0; i < 24; i++) {
            robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
            robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
            robo_css_value_t value = ROBO_CSS_CLEARED;
            int j;

            robo_css_declaration_list_init(&list);
            for (j = 0; j < 5; j++) {
                robo_css_declaration_make(&declaration,
                                          robo_css_slice_from_text("color"),
                                          ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
                ROBO_CHECK_EQ_INT(robo_css_value_make_list(&value, NULL), ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&declaration, &value,
                                                                 NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_css_declaration_list_append(&list, &declaration,
                                                                   NULL),
                                  ROBO_OK);
            }
            ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 5u);
            robo_css_declaration_list_dispose(&list);
            ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        }
    }
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(we_enabled);
}

static void test_css_inline_parse_text_boundary(void)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    const char *text = "color: red; padding: 10px";

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(text, strlen(text),
                                                           ROBO_CSS_ORIGIN_INLINE,
                                                           &list, &error),
                      ROBO_NOT_SUPPORTED);
    /* The single most important property of a not-yet-implemented boundary: it
     * does not pretend. The list is initialised and empty, and the error says
     * exactly why. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 0u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_UNSUPPORTED);
    ROBO_CHECK_EQ_INT(error.code, ROBO_NOT_SUPPORTED);
    ROBO_CHECK(robo_css_error_kind_is_fatal(error.kind));
    ROBO_CHECK(!robo_css_error_kind_is_recoverable(error.kind));
    ROBO_CHECK(robo_css_error_is_error(&error));
    ROBO_CHECK(error.message != NULL);
    ROBO_CHECK(strstr(error.message, "9.3") != NULL);
    /* The list is disposable, so a caller that ignores the result leaks nothing. */
    robo_css_declaration_list_dispose(&list);

    /* Empty text is still refused for the same reason, not treated as "nothing
     * to parse" — there is no parser at all yet. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text("", 0u,
                                                           ROBO_CSS_ORIGIN_INLINE, &list,
                                                           &error),
                      ROBO_NOT_SUPPORTED);
    robo_css_declaration_list_dispose(&list);

    /* A NULL list, or text that does not exist, is a caller mistake. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(text, strlen(text),
                                                           ROBO_CSS_ORIGIN_INLINE, NULL,
                                                           &error),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(NULL, 4u,
                                                           ROBO_CSS_ORIGIN_INLINE, &list,
                                                           &error),
                      ROBO_INVALID_ARGUMENT);
    /* A NULL error record is optional: the result code is authoritative. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(text, strlen(text),
                                                           ROBO_CSS_ORIGIN_INLINE, &list,
                                                           NULL),
                      ROBO_NOT_SUPPORTED);
    robo_css_declaration_list_dispose(&list);

    robo_css_error_init(&error);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK(!robo_css_error_is_error(&error));
    ROBO_CHECK_STR_EQ(robo_css_error_message(&error), "");
    robo_css_error_init(NULL);
}

void robo_test_register_css_declaration(void);

void robo_test_register_css_declaration(void)
{
    robo_test_register("css.property.table_lifecycle",
                       test_css_property_table_lifecycle);
    robo_test_register("css.property.register_and_lookup",
                       test_css_property_register_and_lookup);
    robo_test_register("css.property.rejects_bad_names",
                       test_css_property_rejects_bad_names);
    robo_test_register("css.property.ids_never_reissued",
                       test_css_property_ids_never_reissued);
    robo_test_register("css.declaration.model", test_css_declaration_model);
    robo_test_register("css.declaration.value_and_importance",
                       test_css_declaration_value_and_importance);
    robo_test_register("css.declaration.resolve_property",
                       test_css_declaration_resolve_property);
    robo_test_register("css.declaration.ranges", test_css_declaration_ranges);
    robo_test_register("css.declaration.list", test_css_declaration_list);
    robo_test_register("css.declaration.list_lifetime_balances",
                       test_css_declaration_list_lifetime_balances);
    robo_test_register("css.inline.parse_text_boundary",
                       test_css_inline_parse_text_boundary);
}
