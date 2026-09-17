/*
 * RoboCSS Phase 9.1 component-value suite:
 * the four kinds, nesting, and the move-in ownership rule.
 */

#include "RoboCssTestSupport.h"

static void test_css_value_kinds(void)
{
    robo_css_value_kind_t kind;

    for (kind = ROBO_CSS_VALUE_KIND_NONE;
         kind < (robo_css_value_kind_t)ROBO_CSS_VALUE_KIND_COUNT;
         kind = (robo_css_value_kind_t)(kind + 1)) {
        const char *name = robo_css_value_kind_name(kind);

        ROBO_CHECK(robo_css_value_kind_is_known(kind));
        ROBO_CHECK(name != NULL);
        ROBO_CHECK(strcmp(name, "unknown") != 0);
    }
    ROBO_CHECK(!robo_css_value_kind_is_known((robo_css_value_kind_t)99));
    ROBO_CHECK_STR_EQ(robo_css_value_kind_name((robo_css_value_kind_t)99), "unknown");

    /* A token is a leaf; the other three hold children. */
    ROBO_CHECK(!robo_css_value_kind_holds_children(ROBO_CSS_VALUE_KIND_NONE));
    ROBO_CHECK(!robo_css_value_kind_holds_children(ROBO_CSS_VALUE_KIND_TOKEN));
    ROBO_CHECK(robo_css_value_kind_holds_children(ROBO_CSS_VALUE_KIND_FUNCTION));
    ROBO_CHECK(robo_css_value_kind_holds_children(ROBO_CSS_VALUE_KIND_BLOCK));
    ROBO_CHECK(robo_css_value_kind_holds_children(ROBO_CSS_VALUE_KIND_LIST));
}

static void test_css_value_token(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_value_t value = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "red");
    robo_css_value_init(&value);
    ROBO_CHECK(robo_css_value_is_empty(&value));
    ROBO_CHECK_EQ_INT(value.kind, ROBO_CSS_VALUE_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(&value), 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_depth(&value), 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_tree_count(&value), 0u);

    ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_IDENT, &source,
                                          robo_css_range_span(0u, 3u), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_value_make_token(&value, &token, NULL), ROBO_OK);
    ROBO_CHECK(!robo_css_value_is_empty(&value));
    ROBO_CHECK_EQ_INT(value.kind, ROBO_CSS_VALUE_KIND_TOKEN);
    ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_value_token_text(&value), "red"));
    ROBO_CHECK_EQ_INT(robo_css_value_token(&value).type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(robo_css_value_range(&value).begin, 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_range(&value).end, 3u);
    ROBO_CHECK_EQ_INT(robo_css_value_depth(&value), 1u);
    ROBO_CHECK_EQ_INT(robo_css_value_tree_count(&value), 1u);
    /* A token value has no name and no children, and says so rather than
     * inventing either. */
    ROBO_CHECK(robo_css_slice_is_empty(robo_css_value_name(&value)));
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(&value), 0u);

    ROBO_CHECK_EQ_INT(robo_css_value_make_token(&value, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_css_token_init(&token); /* the NONE token is a known type, so it is legal */
    ROBO_CHECK_EQ_INT(robo_css_value_make_token(&value, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(value.kind, ROBO_CSS_VALUE_KIND_TOKEN);
    ROBO_CHECK_EQ_INT(value.token.type, ROBO_CSS_TOKEN_NONE);

    robo_css_value_dispose(&value);
    ROBO_CHECK(robo_css_value_is_empty(&value));
    ROBO_CHECK_EQ_INT(robo_css_value_make_token(NULL, &token, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_css_source_dispose(&source);
}

static void test_css_value_function_and_block(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_value_t function = ROBO_CSS_CLEARED;
    robo_css_value_t block = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "rgb(1,2,3)");
    robo_css_value_init(&function);
    robo_css_value_init(&block);

    ROBO_CHECK_EQ_INT(robo_css_value_make_function(&function,
                                                   robo_css_slice_from_text("rgb"),
                                                   robo_css_range_span(0u, 10u), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(function.kind, ROBO_CSS_VALUE_KIND_FUNCTION);
    ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_value_name(&function), "rgb"));
    ROBO_CHECK_EQ_INT(robo_css_value_depth(&function), 1u);

    /* A function needs a name; an empty one is refused. */
    ROBO_CHECK_EQ_INT(robo_css_value_make_function(&function, robo_css_slice_empty(),
                                                   robo_css_range_at(0u), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(function.kind, ROBO_CSS_VALUE_KIND_FUNCTION);

    /* A block records the bracket it opened with *and* the one that closes it,
     * so a recovery path can tell a matched block from an unterminated one. */
    ROBO_CHECK_EQ_INT(robo_css_value_make_block(&block, ROBO_CSS_TOKEN_LEFT_SQUARE,
                                                robo_css_range_span(0u, 9u), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(block.kind, ROBO_CSS_VALUE_KIND_BLOCK);
    ROBO_CHECK_EQ_INT(block.open, ROBO_CSS_TOKEN_LEFT_SQUARE);
    ROBO_CHECK_EQ_INT(block.close, ROBO_CSS_TOKEN_RIGHT_SQUARE);
    ROBO_CHECK_EQ_INT(robo_css_value_make_block(&block, ROBO_CSS_TOKEN_SEMICOLON,
                                                robo_css_range_at(0u), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(block.close, ROBO_CSS_TOKEN_RIGHT_SQUARE);

    robo_css_value_dispose(&function);
    robo_css_value_dispose(&block);
    robo_css_source_dispose(&source);
}

static void test_css_value_nesting(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_value_t list = ROBO_CSS_CLEARED;
    robo_css_value_t function = ROBO_CSS_CLEARED;
    robo_css_value_t block = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "translate(1px,2px)");

    /* list > function > (block > token, token): the shape a real declaration
     * value has, built the way the parser will build it — innermost first. */
    robo_css_value_init(&block);
    ROBO_CHECK_EQ_INT(robo_css_value_make_block(&block, ROBO_CSS_TOKEN_LEFT_PAREN,
                                                robo_css_range_span(9u, 5u), NULL),
                      ROBO_OK);
    {
        robo_css_value_t inner = ROBO_CSS_CLEARED;

        css_value_token_at(&inner, &source, "1px", NULL);
        ROBO_CHECK_EQ_INT(robo_css_value_append(&block, &inner, NULL), ROBO_OK);
        ROBO_CHECK(robo_css_value_is_empty(&inner));
        css_value_token_at(&inner, &source, "2px", NULL);
        ROBO_CHECK_EQ_INT(robo_css_value_append(&block, &inner, NULL), ROBO_OK);
    }

    robo_css_value_init(&function);
    ROBO_CHECK_EQ_INT(robo_css_value_make_function(&function,
                                                   robo_css_slice_from_text("translate"),
                                                   robo_css_range_span(0u, 14u), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_value_append(&function, &block, NULL), ROBO_OK);

    robo_css_value_init(&list);
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&list, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_value_append(&list, &function, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_value_child_count(&list), 1u);
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(robo_css_value_child(&list, 0u)), 1u);
    ROBO_CHECK_EQ_INT(
        robo_css_value_child_count(robo_css_value_child(robo_css_value_child(&list, 0u), 0u)),
        2u);
    ROBO_CHECK_EQ_INT(robo_css_value_depth(&list), 4u);
    /* list + function + block + the two tokens = 5 records. */
    ROBO_CHECK_EQ_INT(robo_css_value_tree_count(&list), 5u);
    ROBO_CHECK(robo_css_value_child(&list, 1u) == NULL);
    ROBO_CHECK(robo_css_value_child(NULL, 0u) == NULL);

    robo_css_value_dispose(&list);
    ROBO_CHECK(robo_css_value_is_empty(&list));
    ROBO_CHECK_EQ_INT(robo_css_value_depth(&list), 0u);
    robo_css_source_dispose(&source);
}

static void test_css_value_append_moves(void)
{
    robo_css_value_t list = ROBO_CSS_CLEARED;
    robo_css_value_t child = ROBO_CSS_CLEARED;

    css_fixture();
    robo_css_value_init(&list);
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&list, NULL), ROBO_OK);

    /* A child is moved in, not shared: after the append the caller's value is
     * NONE, so there is exactly one owner and no double free is possible. */
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&child, NULL), ROBO_OK);
    {
        robo_css_value_t grandchild = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_value_make_list(&grandchild, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_value_append(&child, &grandchild, NULL), ROBO_OK);
        ROBO_CHECK(robo_css_value_is_empty(&grandchild));
        ROBO_CHECK_EQ_INT(robo_css_value_child_count(&child), 1u);
    }
    ROBO_CHECK_EQ_INT(robo_css_value_append(&list, &child, NULL), ROBO_OK);
    ROBO_CHECK(robo_css_value_is_empty(&child));
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(&child), 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(&list), 1u);
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(robo_css_value_child(&list, 0u)), 1u);

    /* A refused append leaves the child untouched: it still owns its tree. */
    {
        robo_css_value_t token = ROBO_CSS_CLEARED;
        robo_css_value_t source_value = ROBO_CSS_CLEARED;

        robo_css_value_init(&token);
        robo_css_value_init(&source_value);
        ROBO_CHECK_EQ_INT(robo_css_value_make_list(&source_value, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_value_append(&token, &source_value, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(source_value.kind, ROBO_CSS_VALUE_KIND_LIST);
        robo_css_value_dispose(&source_value);
    }

    robo_css_value_dispose(&list);
}

static void test_css_value_rejects_bad_arguments(void)
{
    robo_css_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t other = ROBO_CSS_CLEARED;
    robo_css_diag_t diag;

    css_fixture();
    robo_css_value_init(&value);
    robo_css_value_init(&other);

    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_value_append(NULL, &other, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_VALUE);
    ROBO_CHECK_EQ_INT(robo_css_value_append(&value, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* A value cannot be its own child: that is how a cycle would start. */
    ROBO_CHECK_EQ_INT(robo_css_value_append(&value, &value, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A TOKEN value holds no children, so appending into it is refused rather
     * than silently turning the leaf into a container. */
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&other, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_value_append(&value, &other, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(other.kind, ROBO_CSS_VALUE_KIND_LIST);

    ROBO_CHECK_EQ_INT(robo_css_value_make_list(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_value_move(NULL, &other, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_value_move(&other, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_value_move(&other, &other, NULL),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK(robo_css_value_is_empty(NULL));
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_depth(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_tree_count(NULL), 0u);
    ROBO_CHECK(robo_css_slice_is_empty(robo_css_value_token_text(NULL)));
    ROBO_CHECK(robo_css_slice_is_empty(robo_css_value_name(NULL)));
    ROBO_CHECK_EQ_INT(robo_css_value_range(NULL).begin, 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_token(NULL).type, ROBO_CSS_TOKEN_NONE);
    robo_css_value_dispose(NULL);
    robo_css_value_clear_children(NULL);

    robo_css_value_dispose(&other);
    robo_css_value_dispose(&value);
}

static void test_css_value_move_transfers_ownership(void)
{
    robo_css_value_t from = ROBO_CSS_CLEARED;
    robo_css_value_t to = ROBO_CSS_CLEARED;
    robo_css_value_t token = ROBO_CSS_CLEARED;
    robo_css_source_t source = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "owned");

    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&from, NULL), ROBO_OK);
    css_value_token_at(&token, &source, "owned", NULL);
    ROBO_CHECK_EQ_INT(robo_css_value_append(&from, &token, NULL), ROBO_OK);

    /* Moving into a value that already holds a tree disposes that tree: the
     * destination never accumulates two owners. */
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&to, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_value_move(&from, &to, NULL), ROBO_OK);
    ROBO_CHECK(robo_css_value_is_empty(&from));
    ROBO_CHECK_EQ_INT(to.kind, ROBO_CSS_VALUE_KIND_LIST);
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(&to), 1u);
    /* The moved child is the token that was appended, byte for byte. */
    ROBO_CHECK(robo_css_slice_equals_ascii(
        robo_css_value_token_text(robo_css_value_child(&to, 0u)), "owned"));

    /* Moving an empty value empties the destination, which is the honest
     * meaning of "move this nothing here". */
    ROBO_CHECK_EQ_INT(robo_css_value_move(&from, &to, NULL), ROBO_OK);
    ROBO_CHECK(robo_css_value_is_empty(&to));

    robo_css_value_dispose(&to);
    robo_css_value_dispose(&from);
    robo_css_source_dispose(&source);
}

static void test_css_value_lifetime_balances(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    int we_enabled;
    size_t baseline;

    css_fixture();
    css_source_of_string(&source, "4px");
    we_enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();
    {
        int i;

        for (i = 0; i < 32; i++) {
            robo_css_value_t list = ROBO_CSS_CLEARED;
            robo_css_value_t function = ROBO_CSS_CLEARED;
            robo_css_value_t block = ROBO_CSS_CLEARED;
            robo_css_value_t token = ROBO_CSS_CLEARED;
            int j;

            ROBO_CHECK_EQ_INT(robo_css_value_make_list(&list, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_value_make_function(&function,
                                                           robo_css_slice_from_text("f"),
                                                           robo_css_range_at(0u), NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_value_make_block(&block,
                                                        ROBO_CSS_TOKEN_LEFT_CURLY,
                                                        robo_css_range_at(0u), NULL),
                              ROBO_OK);
            for (j = 0; j < 4; j++) {
                css_value_token_at(&token, &source, "4px", NULL);
                ROBO_CHECK_EQ_INT(robo_css_value_append(&block, &token, NULL),
                                  ROBO_OK);
            }
            ROBO_CHECK_EQ_INT(robo_css_value_append(&function, &block, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_value_append(&list, &function, NULL), ROBO_OK);
            /* list + function + block + four tokens = 7 records; four levels. */
            ROBO_CHECK_EQ_INT(robo_css_value_tree_count(&list), 7u);
            ROBO_CHECK_EQ_INT(robo_css_value_depth(&list), 4u);

            /* clear_children frees the tree but keeps the value usable. */
            robo_css_value_clear_children(&list);
            ROBO_CHECK_EQ_INT(robo_css_value_child_count(&list), 0u);
            ROBO_CHECK_EQ_INT(list.kind, ROBO_CSS_VALUE_KIND_LIST);
            robo_css_value_dispose(&list);
            ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        }
    }
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(we_enabled);
    robo_css_source_dispose(&source);
}

void robo_test_register_css_value(void);

void robo_test_register_css_value(void)
{
    robo_test_register("css.value.kinds", test_css_value_kinds);
    robo_test_register("css.value.token", test_css_value_token);
    robo_test_register("css.value.function_and_block", test_css_value_function_and_block);
    robo_test_register("css.value.nesting", test_css_value_nesting);
    robo_test_register("css.value.append_moves", test_css_value_append_moves);
    robo_test_register("css.value.rejects_bad_arguments",
                       test_css_value_rejects_bad_arguments);
    robo_test_register("css.value.move_transfers_ownership",
                       test_css_value_move_transfers_ownership);
    robo_test_register("css.value.lifetime_balances", test_css_value_lifetime_balances);
}
