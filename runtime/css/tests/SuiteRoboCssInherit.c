/*
 * RoboCSS inheritance model (Phase 9.7) — css.inherit.*
 *
 * The model without the resolver: how a value is classified as a CSS-wide
 * keyword, how provenance is named, how a resolved style is looked through, and
 * how the value copy underneath all of it behaves.
 *
 * The keyword table is the part worth pinning hardest, because the *negative*
 * cases are the ones that matter. Recognising `inherit` is easy; refusing to
 * recognise `inherit inherit`, `calc(inherit)` or a bare `red` is what keeps a
 * resolution from inventing a value the author never wrote, and a table that
 * matched on "contains the word inherit" would pass every positive test here.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Keyword classification                                              */
/* ------------------------------------------------------------------ */

/** Classify the value a declaration's text carries in @p text. */
static robo_css_wide_keyword_t css_keyword_of(const char *text)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_wide_keyword_t keyword = ROBO_CSS_WIDE_KEYWORD_NONE;

    /* Parsed, not guessed: a keyword is recognised on the real value tree, so
     * the test cannot pass by reading bytes the way a wrong implementation
     * might. */
    ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);
    if (robo_css_declaration_list_count(&list) == 1u) {
        keyword = robo_css_wide_keyword_of(
            robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u)));
    }
    robo_css_declaration_list_dispose(&list);
    return keyword;
}

static void test_css_inherit_wide_keywords(void)
{
    css_fixture();

    ROBO_CHECK_EQ_INT(css_keyword_of("color: inherit"), ROBO_CSS_WIDE_KEYWORD_INHERIT);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: initial"), ROBO_CSS_WIDE_KEYWORD_INITIAL);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: unset"), ROBO_CSS_WIDE_KEYWORD_UNSET);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: revert"), ROBO_CSS_WIDE_KEYWORD_REVERT);
    /* Case is irrelevant for a keyword, as it is for every CSS identifier. */
    ROBO_CHECK_EQ_INT(css_keyword_of("color: INHERIT"), ROBO_CSS_WIDE_KEYWORD_INHERIT);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: InHeRiT"), ROBO_CSS_WIDE_KEYWORD_INHERIT);
    /* An escape changes which bytes spell a name, never which name it is. */
    ROBO_CHECK_EQ_INT(css_keyword_of("color: \\69 nherit"), ROBO_CSS_WIDE_KEYWORD_INHERIT);

    /* And now the half that keeps a resolution honest: everything that is not a
     * single identifier is not a keyword. */
    ROBO_CHECK_EQ_INT(css_keyword_of("color: red"), ROBO_CSS_WIDE_KEYWORD_NONE);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: auto"), ROBO_CSS_WIDE_KEYWORD_NONE);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: inherit inherit"),
                      ROBO_CSS_WIDE_KEYWORD_NONE);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: red inherit"), ROBO_CSS_WIDE_KEYWORD_NONE);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: calc(inherit)"), ROBO_CSS_WIDE_KEYWORD_NONE);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: var(--x)"), ROBO_CSS_WIDE_KEYWORD_NONE);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: \"inherit\""), ROBO_CSS_WIDE_KEYWORD_NONE);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: 12px"), ROBO_CSS_WIDE_KEYWORD_NONE);
    ROBO_CHECK_EQ_INT(css_keyword_of("color: inherit!"), ROBO_CSS_WIDE_KEYWORD_NONE);
    /* `inherit` before `!important` is the keyword *with* importance, which is
     * a surprising spelling that must keep working: the flag is taken off the
     * value before a keyword is looked for. */
    ROBO_CHECK_EQ_INT(css_keyword_of("color: inherit !important"),
                      ROBO_CSS_WIDE_KEYWORD_INHERIT);
    /* An empty value is not a keyword — and not a value either. */
    ROBO_CHECK_EQ_INT(css_keyword_of("color: "), ROBO_CSS_WIDE_KEYWORD_NONE);

    /* A hand-built value is a shape the parser never produces, so the API has to
     * be told what it does with one rather than left to guess. */
    ROBO_CHECK_EQ_INT(robo_css_wide_keyword_of(NULL), ROBO_CSS_WIDE_KEYWORD_NONE);
    {
        robo_css_value_t empty = ROBO_CSS_CLEARED;

        robo_css_value_init(&empty);
        ROBO_CHECK_EQ_INT(robo_css_wide_keyword_of(&empty), ROBO_CSS_WIDE_KEYWORD_NONE);
        robo_css_value_dispose(&empty);
    }
    {
        /* A bare IDENT token — the shape a hand-built declaration carries. The
         * value accessor reads the token's own text, so the bytes have to be
         * real: a token made with no source is not a token and is refused. */
        robo_css_value_t token = ROBO_CSS_CLEARED;
        robo_css_token_t built = ROBO_CSS_CLEARED;
        robo_css_source_t source = ROBO_CSS_CLEARED;

        css_source_of_literal(&source, "initial");
        css_token_of_text(&built, ROBO_CSS_TOKEN_IDENT, &source, "initial");
        ROBO_CHECK_EQ_INT(robo_css_value_make_token(&token, &built, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_wide_keyword_of(&token),
                          ROBO_CSS_WIDE_KEYWORD_INITIAL);
        robo_css_value_dispose(&token);
    }
}

static void test_css_inherit_keyword_names(void)
{
    ROBO_CHECK_STR_EQ(robo_css_wide_keyword_name(ROBO_CSS_WIDE_KEYWORD_NONE), "none");
    ROBO_CHECK_STR_EQ(robo_css_wide_keyword_name(ROBO_CSS_WIDE_KEYWORD_INHERIT), "inherit");
    ROBO_CHECK_STR_EQ(robo_css_wide_keyword_name(ROBO_CSS_WIDE_KEYWORD_INITIAL), "initial");
    ROBO_CHECK_STR_EQ(robo_css_wide_keyword_name(ROBO_CSS_WIDE_KEYWORD_UNSET), "unset");
    ROBO_CHECK_STR_EQ(robo_css_wide_keyword_name(ROBO_CSS_WIDE_KEYWORD_REVERT), "revert");
    ROBO_CHECK_STR_EQ(robo_css_wide_keyword_name((robo_css_wide_keyword_t)77), "unknown");
    ROBO_CHECK_EQ_INT(robo_css_wide_keyword_is_known(ROBO_CSS_WIDE_KEYWORD_NONE), 1);
    ROBO_CHECK_EQ_INT(robo_css_wide_keyword_is_known(ROBO_CSS_WIDE_KEYWORD_REVERT), 1);
    ROBO_CHECK_EQ_INT(robo_css_wide_keyword_is_known((robo_css_wide_keyword_t)77), 0);
    ROBO_CHECK_EQ_INT(ROBO_CSS_WIDE_KEYWORD_COUNT, 5);
}

static void test_css_inherit_source_names(void)
{
    ROBO_CHECK_STR_EQ(robo_css_inherited_source_name(ROBO_CSS_INHERITED_SOURCE_NONE),
                      "none");
    ROBO_CHECK_STR_EQ(robo_css_inherited_source_name(ROBO_CSS_INHERITED_SOURCE_DECLARED),
                      "declared");
    ROBO_CHECK_STR_EQ(robo_css_inherited_source_name(ROBO_CSS_INHERITED_SOURCE_PARENT),
                      "parent");
    ROBO_CHECK_STR_EQ(robo_css_inherited_source_name(ROBO_CSS_INHERITED_SOURCE_DEFAULT),
                      "default");
    ROBO_CHECK_STR_EQ(robo_css_inherited_source_name(ROBO_CSS_INHERITED_SOURCE_DEFERRED),
                      "deferred");
    ROBO_CHECK_STR_EQ(robo_css_inherited_source_name((robo_css_inherited_source_t)77),
                      "unknown");
    ROBO_CHECK_EQ_INT(robo_css_inherited_source_is_known(ROBO_CSS_INHERITED_SOURCE_NONE),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_inherited_source_is_known(ROBO_CSS_INHERITED_SOURCE_DEFERRED),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_inherited_source_is_known((robo_css_inherited_source_t)77),
                      0);
    ROBO_CHECK_EQ_INT(ROBO_CSS_INHERITED_SOURCE_COUNT, 5);
}

/* ------------------------------------------------------------------ */
/* The resolved style                                                  */
/* ------------------------------------------------------------------ */

static void test_css_inherit_style_lifecycle(void)
{
    robo_css_inherited_style_t style = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t other = ROBO_CSS_CLEARED;

    css_fixture();
    robo_css_inherited_style_init(&style);
    ROBO_CHECK_EQ_INT((int)robo_css_inherited_style_count(&style), 0);
    ROBO_CHECK(robo_css_inherited_style_at(&style, 0u) == NULL);
    ROBO_CHECK(robo_css_inherited_style_find(&style, robo_css_slice_from_text("color")) ==
               NULL);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_has(&style,
                                                  robo_css_slice_from_text("color")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_element(&style),
                      ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_has_parent(&style), 0);
    /* An empty style is a *complete* answer, not an incomplete one: nothing was
     * asked and nothing was refused. */
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_is_complete(&style), 1);

    /* Every accessor tolerates NULL the same way the rest of the model does. */
    ROBO_CHECK_EQ_INT((int)robo_css_inherited_style_count(NULL), 0);
    ROBO_CHECK(robo_css_inherited_style_at(NULL, 0u) == NULL);
    ROBO_CHECK(robo_css_inherited_style_find(NULL, robo_css_slice_from_text("x")) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_is_complete(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_element(NULL), ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_has_parent(NULL), 0);
    ROBO_CHECK(robo_css_inherited_property_name(NULL) .bytes == NULL);
    ROBO_CHECK(robo_css_inherited_property_value(NULL) == NULL);

    /* Disposal is idempotent, and a record left cleared after it is reusable —
     * the record-lifetime rule every RoboCSS type follows. */
    robo_css_inherited_style_dispose(&style);
    robo_css_inherited_style_dispose(&style);
    robo_css_inherited_style_dispose(NULL);
    ROBO_CHECK_EQ_INT((int)robo_css_inherited_style_count(&style), 0);

    /* Moving one by value, which is what the context door does, must not
     * double-free: the source is re-initialised after the move. */
    other = style;
    robo_css_inherited_style_init(&style);
    robo_css_inherited_style_dispose(&other);
    robo_css_inherited_style_dispose(&style);
}

/* ------------------------------------------------------------------ */
/* The value copy                                                      */
/* ------------------------------------------------------------------ */

static void test_css_inherit_value_copy(void)
{
    robo_css_value_t original = ROBO_CSS_CLEARED;
    robo_css_value_t copy = ROBO_CSS_CLEARED;
    char shape[256];

    css_fixture();

    /* A four-level tree: list -> function -> block -> token. Deep enough that a
     * shallow copy would be visible in the shape. */
    {
        robo_css_value_t function = ROBO_CSS_CLEARED;
        robo_css_value_t block = ROBO_CSS_CLEARED;
        robo_css_value_t leaf = ROBO_CSS_CLEARED;
        robo_css_token_t token = ROBO_CSS_CLEARED;
        robo_css_source_t source = ROBO_CSS_CLEARED;

        css_source_of_literal(&source, "1");
        ROBO_CHECK_EQ_INT(robo_css_value_make_list(&original, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_value_make_function(&function,
                                                      robo_css_slice_from_text("calc"),
                                                      robo_css_range_at(0u), NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_value_make_block(&block, ROBO_CSS_TOKEN_LEFT_SQUARE,
                                                   robo_css_range_at(0u), NULL),
                          ROBO_OK);
        css_token_of_text(&token, ROBO_CSS_TOKEN_NUMBER, &source, "1");
        ROBO_CHECK_EQ_INT(robo_css_value_make_token(&leaf, &token, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_value_append(&block, &leaf, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_value_append(&function, &block, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_value_append(&original, &function, NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT((int)robo_css_value_depth(&original), 4);
    ROBO_CHECK_EQ_INT((int)robo_css_value_tree_count(&original), 4);

    ROBO_CHECK_EQ_INT(robo_css_value_copy(&copy, &original, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_css_value_depth(&copy), 4);
    ROBO_CHECK_EQ_INT((int)robo_css_value_tree_count(&copy), 4);
    css_shape(&copy, shape, sizeof(shape));
    /* The token renders as its own bytes — the shape writer prints what a token
     * covers, not its type — so a NUMBER token over `1` is `1` here. */
    ROBO_CHECK_STR_EQ(shape, "calc([1])");

    /* The copy is *independent*: emptying the copy's children must leave the
     * original intact. This is the property that makes a child's inherited value
     * safe from the parent's disposal, and a shallow copy would fail it. */
    ROBO_CHECK_EQ_INT(robo_css_value_copy(&copy, &original, NULL), ROBO_OK);
    {
        /* Reach into the copy the way a mutation would, and prove the original
         * did not move. */
        robo_css_value_t *copied_child = &copy.children.items[0];

        ROBO_CHECK_EQ_INT(copied_child != &original.children.items[0], 1);
        ROBO_CHECK_EQ_INT((int)robo_css_value_child_count(copied_child), 1);
        robo_css_value_clear_children(copied_child);
        ROBO_CHECK_EQ_INT((int)robo_css_value_child_count(copied_child), 0);
    }
    css_shape(&original, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "calc([1])");

    /* Disposing the copy leaves the original alone, and the other way round. */
    robo_css_value_dispose(&copy);
    ROBO_CHECK_EQ_INT(robo_css_value_is_empty(&copy), 1);
    css_shape(&original, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "calc([1])");
    ROBO_CHECK_EQ_INT(robo_css_value_copy(&copy, &original, NULL), ROBO_OK);
    robo_css_value_dispose(&original);
    ROBO_CHECK_EQ_INT((int)robo_css_value_depth(&copy), 4);
    robo_css_value_dispose(&copy);

    /* Recopying into a value that already holds a tree replaces it rather than
     * leaking it — the rule every RoboCSS "set" call follows. */
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&original, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_value_copy(&copy, &original, NULL), ROBO_OK);
    {
        robo_css_value_t leaf = ROBO_CSS_CLEARED;
        robo_css_token_t token = ROBO_CSS_CLEARED;
        robo_css_source_t source = ROBO_CSS_CLEARED;

        css_source_of_literal(&source, "a");
        css_token_of_text(&token, ROBO_CSS_TOKEN_IDENT, &source, "a");
        ROBO_CHECK_EQ_INT(robo_css_value_make_token(&leaf, &token, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_value_append(&original, &leaf, NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_css_value_copy(&copy, &original, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_css_value_tree_count(&copy), 2);

    /* The argument contract. */
    ROBO_CHECK_EQ_INT(robo_css_value_copy(NULL, &original, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_value_copy(&copy, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_value_copy(&copy, &copy, NULL), ROBO_INVALID_ARGUMENT);
    robo_css_value_dispose(&original);
    robo_css_value_dispose(&copy);
}

static void test_css_inherit_value_copy_balances(void)
{
    int enabled;
    long baseline;

    css_fixture();
    enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();

    {
        robo_css_value_t list = ROBO_CSS_CLEARED;
        robo_css_value_t copy = ROBO_CSS_CLEARED;
        robo_css_source_t source = ROBO_CSS_CLEARED;
        size_t index;

        css_source_of_literal(&source, "a");
        ROBO_CHECK_EQ_INT(robo_css_value_make_list(&list, NULL), ROBO_OK);
        for (index = 0u; index < 32u; index++) {
            robo_css_value_t leaf = ROBO_CSS_CLEARED;
            robo_css_token_t token = ROBO_CSS_CLEARED;

            css_token_of_text(&token, ROBO_CSS_TOKEN_IDENT, &source, "a");
            ROBO_CHECK_EQ_INT(robo_css_value_make_token(&leaf, &token, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_value_append(&list, &leaf, NULL), ROBO_OK);
        }
        /* Copy over and over: each copy disposes the last, so the live count
         * must not creep. */
        for (index = 0u; index < 50u; index++) {
            ROBO_CHECK_EQ_INT(robo_css_value_copy(&copy, &list, NULL), ROBO_OK);
        }
        ROBO_CHECK_EQ_INT((int)robo_css_value_tree_count(&copy), 33);
        robo_css_value_dispose(&copy);
        robo_css_value_dispose(&list);
    }

    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(enabled);
}

void robo_test_register_css_inherit(void);

void robo_test_register_css_inherit(void)
{
    robo_test_register("css.inherit.wide_keywords", test_css_inherit_wide_keywords);
    robo_test_register("css.inherit.keyword_names", test_css_inherit_keyword_names);
    robo_test_register("css.inherit.source_names", test_css_inherit_source_names);
    robo_test_register("css.inherit.style_lifecycle", test_css_inherit_style_lifecycle);
    robo_test_register("css.inherit.value_copy", test_css_inherit_value_copy);
    robo_test_register("css.inherit.value_copy_balances",
                       test_css_inherit_value_copy_balances);
}
