/*
 * RoboCSS parser: declaration values (Phase 9.3).
 *
 * A declaration's value is the one place in RoboCSS where the model is a *tree*
 * rather than a record, and that is the whole reason 9.2 kept a token's spelling
 * separate from its value: `calc(100% - 20px)` has to survive as a function with
 * four children, because 9.10's arithmetic and 9.11's box model both need the
 * structure and neither can get it back from a string.
 *
 * What these tests are looking for, in one sentence each:
 *
 *   - every byte of a value is accounted for as a component value, and the
 *     value's range is exactly the bytes it covers;
 *   - `(`, `[` and `{` nest, and the value records which closing token matched;
 *   - nesting is bounded, and past the bound the construct goes flat rather than
 *     taking the stack down with it.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Component values                                                    */
/* ------------------------------------------------------------------ */

static void test_css_parse_value_component_vocabulary(void)
{
    static const struct {
        const char *value;
        const char *shape;
    } cases[] = {
        { "red", "red" },
        { "1", "1" },
        { "1.5", "1.5" },
        { "-10", "-10" },
        { "+10", "+10" },
        { "50%", "50%" },
        { "10px", "10px" },
        { "1e3", "1e3" },
        { "#fff", "#fff" },
        { "\"text\"", "\"text\"" },
        { "/", "/" },
        { "a ! b", "a,_,!,_,b" },
        { "auto 0 auto", "auto,_,0,_,auto" },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[64];
        char shape[64];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;

        /* Build `x: <case>` in a buffer the shape helper can read. */
        snprintf(text, sizeof(text), "x: %s", cases[index].value);
        ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);
        css_declaration_shape(&list, 0u, shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, cases[index].shape);
        robo_css_declaration_list_dispose(&list);
    }
}

static void test_css_parse_value_tokens_keep_their_ranges(void)
{
    static const char *text = "p { color: #06c; }";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_value_t *value = robo_css_declaration_value(
        robo_css_style_rule_declaration(css_sheet_style_rule(sheet, 0u), 0u));
    const robo_css_value_t *token;

    ROBO_CHECK(value != NULL);
    ROBO_CHECK_EQ_INT(value->kind, ROBO_CSS_VALUE_KIND_LIST);
    ROBO_CHECK_EQ_INT(value->children.count, 1u);
    token = &value->children.items[0];
    ROBO_CHECK_EQ_INT(token->kind, ROBO_CSS_VALUE_KIND_TOKEN);
    ROBO_CHECK_EQ_INT(token->token.type, ROBO_CSS_TOKEN_HASH);
    /* `#06c` is a hash the tokenizer classified *unrestricted*, because an
     * ident-headed name is what makes a hash an id. 9.3 preserved the
     * classification and did not second-guess it: `#06c` is a valid colour and
     * not a valid selector, and which reading applies is 9.4's or 9.9's. */
    ROBO_CHECK_EQ_INT(token->token.hash, ROBO_CSS_HASH_TYPE_UNRESTRICTED);

    /* The hash's own range is 11..15 — `#06c` — and the token's text is those
     * same bytes, so a caller can hand either to a diagnostic and get the same
     * answer. */
    ROBO_CHECK_EQ_INT(token->token.range.begin, 11u);
    ROBO_CHECK_EQ_INT(token->token.range.end, 15u);
    ROBO_CHECK_EQ_INT(robo_css_value_range(value).begin, 11u);
    ROBO_CHECK_EQ_INT(robo_css_value_range(value).end, 15u);
    ROBO_CHECK_EQ_INT(token->token.text.length, 4u);
    ROBO_CHECK(memcmp(token->token.text.bytes, "#06c", 4u) == 0);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Functions                                                           */
/* ------------------------------------------------------------------ */

static void test_css_parse_value_functions(void)
{
    static const struct {
        const char *value;
        const char *name;
        size_t children;
    } cases[] = {
        { "rgb(1, 2, 3)", "rgb", 7u },
        { "calc(1px)", "calc", 1u },
        { "var(--x)", "var", 1u },
        { "max()", "max", 0u },
        /* The function's *name* keeps the author's spelling; ASCII-case
         * insensitivity is a property of the function lookup, and there is no
         * function table in this build. */
        { "RGB(1)", "RGB", 1u },
        { "translate(-50%)", "translate", 1u },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[96];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        const robo_css_value_t *value;
        const robo_css_value_t *function;

        snprintf(text, sizeof(text), "x: %s", cases[index].value);
        ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);
        value = robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u));
        ROBO_CHECK_EQ_INT(value->children.count, 1u);
        function = &value->children.items[0];
        ROBO_CHECK_EQ_INT(function->kind, ROBO_CSS_VALUE_KIND_FUNCTION);
        ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_value_name(function),
                                              cases[index].name));
        ROBO_CHECK_EQ_INT(robo_css_value_child_count(function), cases[index].children);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        robo_css_declaration_list_dispose(&list);
    }
}

static void test_css_parse_value_nested_functions(void)
{
    static const char *text = "width: calc(100% - var(--gutter, 20px));";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *calc;
    const robo_css_value_t *var;
    char shape[128];

    ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);
    calc = &robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u))
                ->children.items[0];
    ROBO_CHECK_EQ_INT(calc->kind, ROBO_CSS_VALUE_KIND_FUNCTION);
    ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_value_name(calc), "calc"));

    /* The `var()` is a *child of the calc*, one level down and inside the
     * arithmetic — which is the one thing a string value could not express.
     * `100% - var(...)` is five children: the two operands, the operator and the
     * whitespace that separates them. */
    ROBO_CHECK_EQ_INT(calc->children.count, 5u);
    var = &calc->children.items[4];
    ROBO_CHECK_EQ_INT(var->kind, ROBO_CSS_VALUE_KIND_FUNCTION);
    ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_value_name(var), "var"));
    /* `--gutter`, the `,`, the space and `20px` are four children: the comma and
     * the whitespace survive as component values here because nothing has said
     * they are separators yet. Trimming them is the grammar that later *reads*
     * `var()`'s arguments, and that grammar is not this phase's. */
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(var), 4u);

    css_shape(robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u)),
              shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "calc(100%,_,-,_,var(--gutter,,,_,20px))");

    robo_css_declaration_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Blocks                                                              */
/* ------------------------------------------------------------------ */

static void test_css_parse_value_blocks(void)
{
    static const struct {
        const char *value;
        const char *shape;
    } cases[] = {
        /* `a[b]` is two component values: an IDENT and a block. They are not
         * one thing until a selector parser says so. */
        { "a[b]", "a,[b]" },
        { "url(x)", "url(x)" },
        { "{}", "{}" },
        { "(a)(b)", "(a),(b)" },
        { "[a(b)]", "[a(b)]" },
        { "{ a: b }", "{_,a,:,_,b,_}" },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[96];
        char shape[96];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;

        snprintf(text, sizeof(text), "x: %s", cases[index].value);
        ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);
        css_declaration_shape(&list, 0u, shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, cases[index].shape);
        robo_css_declaration_list_dispose(&list);
    }
}

static void test_css_parse_value_blocks_record_their_matching_close(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value;
    const robo_css_value_t *square;
    const robo_css_value_t *paren;
    const robo_css_value_t *unmatched;

    ROBO_CHECK_EQ_INT(css_parse_declarations("x: a[1] (2) [3", &list, &error),
                      ROBO_OK);
    value = robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u));
    /* `a`, `[1]`, space, `(2)`, space, `[3` — six children. `a` and `[1]` are
     * adjacent in the source, so there is no whitespace token between them; the
     * whitespace that *is* there is a child like any other, which is why the
     * blocks sit at 1, 3 and 5 rather than 0, 1 and 2. */
    ROBO_CHECK_EQ_INT(value->children.count, 6u);

    square = &value->children.items[1];
    ROBO_CHECK_EQ_INT(square->kind, ROBO_CSS_VALUE_KIND_BLOCK);
    ROBO_CHECK_EQ_INT(square->open, ROBO_CSS_TOKEN_LEFT_SQUARE);
    ROBO_CHECK_EQ_INT(square->close, ROBO_CSS_TOKEN_RIGHT_SQUARE);

    paren = &value->children.items[3];
    ROBO_CHECK_EQ_INT(paren->kind, ROBO_CSS_VALUE_KIND_BLOCK);
    ROBO_CHECK_EQ_INT(paren->open, ROBO_CSS_TOKEN_LEFT_PAREN);
    ROBO_CHECK_EQ_INT(paren->close, ROBO_CSS_TOKEN_RIGHT_PAREN);

    /* The last block never closed. It is still a block, still holds its
     * contents, and its `close` is NONE — "this was left open" is a fact a
     * later phase can act on, and inventing a `]` would hide it. */
    unmatched = &value->children.items[5];
    ROBO_CHECK_EQ_INT(unmatched->kind, ROBO_CSS_VALUE_KIND_BLOCK);
    ROBO_CHECK_EQ_INT(unmatched->open, ROBO_CSS_TOKEN_LEFT_SQUARE);
    ROBO_CHECK_EQ_INT(unmatched->close, ROBO_CSS_TOKEN_NONE);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF);

    robo_css_declaration_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* The nesting bound                                                   */
/* ------------------------------------------------------------------ */

static void test_css_parse_value_nesting_bound(void)
{
    char text[1024];
    size_t offset = 0u;
    size_t depth;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;

    /* ROBO_CSS_MAX_NESTING + 8 functions, one inside the other. The bound is
     * what keeps adversarial input from being a stack overflow, and what these
     * assertions are for is that hitting it is *reported* and the parse still
     * ends at a defined place with a dispose-able tree. */
    offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "x: ");
    for (depth = 0u; depth < ROBO_CSS_MAX_NESTING + 8u; depth++) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "f(");
    }
    offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "1");
    for (depth = 0u; depth < ROBO_CSS_MAX_NESTING + 8u; depth++) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, ")");
    }

    ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX);
    ROBO_CHECK(error.position.offset > 0u);

    /* The tree is at most as deep as the bound allowed, and it is a tree the
     * disposer can walk: this call is the assertion. */
    {
        const robo_css_value_t *value =
            robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u));

        ROBO_CHECK(value != NULL);
    }
    robo_css_declaration_list_dispose(&list);

    /* And a depth the bound *does* allow builds the whole thing. */
    {
        char shallow[512];
        size_t at = 0u;
        size_t level;

        at += (size_t)snprintf(shallow + at, sizeof(shallow) - at, "x: ");
        for (level = 0u; level < 16u; level++) {
            at += (size_t)snprintf(shallow + at, sizeof(shallow) - at, "f(");
        }
        at += (size_t)snprintf(shallow + at, sizeof(shallow) - at, "1");
        for (level = 0u; level < 16u; level++) {
            at += (size_t)snprintf(shallow + at, sizeof(shallow) - at, ")");
        }

        ROBO_CHECK_EQ_INT(css_parse_declarations(shallow, &list, &error), ROBO_OK);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        {
            const robo_css_value_t *value =
                robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u));
            const robo_css_value_t *function = value;
            size_t seen = 0u;

            while (function->kind == ROBO_CSS_VALUE_KIND_LIST) {
                function = &function->children.items[0];
            }
            while (function->kind == ROBO_CSS_VALUE_KIND_FUNCTION) {
                seen++;
                function = &function->children.items[0];
            }
            ROBO_CHECK_EQ_INT(seen, 16u);
            ROBO_CHECK_EQ_INT(function->kind, ROBO_CSS_VALUE_KIND_TOKEN);
        }
        robo_css_declaration_list_dispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* Strings and urls                                                    */
/* ------------------------------------------------------------------ */

static void test_css_parse_value_strings(void)
{
    static const struct {
        const char *value;
        const char *shape;
    } cases[] = {
        { "\"text\"", "\"text\"" },
        { "'text'", "'text'" },
        { "\"\"", "\"\"" },
        /* The shape folds whitespace to `_`, and a string's own text is not an
         * exception: `"a b"` shapes as `"a_b"`. The *token* still holds the
         * space — that is what the next assertion is for. */
        { "\"a b\"", "\"a_b\"" },
        { "\"a\\\"b\"", "\"a\\\"b\"" },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[64];
        char shape[64];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;

        snprintf(text, sizeof(text), "x: %s", cases[index].value);
        ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        css_declaration_shape(&list, 0u, shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, cases[index].shape);
        robo_css_declaration_list_dispose(&list);
    }

    /* The two ways a string can end badly are two different tokens, and the
     * difference is the specification's: a *newline* inside a string makes it a
     * BAD_STRING (never valid), while *end of input* returns the string token
     * with the error recorded, because a truncated stylesheet is not the same as
     * a malformed one. The parser keeps whichever it was given. */
    {
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        const robo_css_value_t *value;

        ROBO_CHECK_EQ_INT(css_parse_declarations("x: \"unterminated", &list, &error),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);
        value = robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u));
        ROBO_CHECK_EQ_INT(value->children.items[0].token.type, ROBO_CSS_TOKEN_STRING);
        ROBO_CHECK_EQ_INT(value->children.items[0].token.closed, 0);
        robo_css_declaration_list_dispose(&list);

        ROBO_CHECK_EQ_INT(css_parse_declarations("x: \"broken\nmore", &list, &error),
                          ROBO_OK);
        value = robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u));
        ROBO_CHECK_EQ_INT(value->children.items[0].token.type,
                          ROBO_CSS_TOKEN_BAD_STRING);
        robo_css_declaration_list_dispose(&list);
    }

    /* And a string's interior whitespace is content, not a separator: the shape
     * helper folds it for readability, so this is the assertion that the token
     * itself kept it. */
    {
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        const robo_css_value_t *value;
        uint8_t raw[16];
        size_t length = 0u;

        ROBO_CHECK_EQ_INT(css_parse_declarations("x: \"a b\"", &list, &error),
                          ROBO_OK);
        value = robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u));
        ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&value->children.items[0].token, raw,
                                                   sizeof(raw), &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(length, 3u);
        ROBO_CHECK(memcmp(raw, "a b", 3u) == 0);
        (void)0;
        robo_css_declaration_list_dispose(&list);
    }
}

static void test_css_parse_value_url_tokens(void)
{
    static const struct {
        const char *value;
        int closed;
    } cases[] = {
        /* An unquoted url is one *token*, and the tokenizer's own test says so;
         * 9.3 preserved it rather than re-reading the bytes. Nothing was
         * fetched, nothing was resolved, no file was opened. */
        { "url(image.png)", 1 },
        { "url()", 1 },
        /* End of input does not close the url, and the specification is explicit
         * that it is still a url — `closed` is where that fact lives. */
        { "url(broken", 0 },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[96];
        char shape[96];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        const robo_css_value_t *value;

        snprintf(text, sizeof(text), "x: %s", cases[index].value);
        ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);

        value = robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u));
        ROBO_CHECK_EQ_INT(value->children.count, 1u);
        ROBO_CHECK_EQ_INT(value->children.items[0].kind, ROBO_CSS_VALUE_KIND_TOKEN);
        ROBO_CHECK_EQ_INT(value->children.items[0].token.type, ROBO_CSS_TOKEN_URL);
        ROBO_CHECK_EQ_INT(value->children.items[0].token.closed, cases[index].closed);
        css_declaration_shape(&list, 0u, shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, cases[index].value);
        robo_css_declaration_list_dispose(&list);
    }
}

static void test_css_parse_value_url_quoted_is_a_function(void)
{
    static const char *cases[] = {
        "url(\"image.png\")",
        "url('image.png')",
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[96];
        char shape[96];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        const robo_css_value_t *value;
        const robo_css_value_t *function;

        /* A *quoted* url is a function, not a url token: the specification forks
         * on the quote, and the tokenizer (9.2) is where that fork belongs. The
         * parser's job is to keep whichever it was given, so this asserts the
         * function's name and its string child. */
        snprintf(text, sizeof(text), "x: %s", cases[index]);
        ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);
        value = robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u));
        ROBO_CHECK_EQ_INT(value->children.count, 1u);
        function = &value->children.items[0];
        ROBO_CHECK_EQ_INT(function->kind, ROBO_CSS_VALUE_KIND_FUNCTION);
        ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_value_name(function), "url"));
        ROBO_CHECK_EQ_INT(robo_css_value_child_count(function), 1u);
        ROBO_CHECK_EQ_INT(function->children.items[0].kind, ROBO_CSS_VALUE_KIND_TOKEN);
        ROBO_CHECK_EQ_INT(function->children.items[0].token.type, ROBO_CSS_TOKEN_STRING);
        css_declaration_shape(&list, 0u, shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, cases[index]);
        robo_css_declaration_list_dispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void test_css_parse_value_lifetime(void)
{
    static const char *text =
        "p { background: url(\"a.png\") no-repeat center / cover;"
        "    width: calc(100% - var(--x, 2px));"
        "    font-family: \"A\", \"B\", sans-serif; }";
    int we_enabled = css_allocator_ensure_enabled();
    size_t baseline = robo_mem_debug_live_count();
    int round;

    /* Every round builds and destroys a value tree with blocks, functions,
     * strings and urls in it. A leaked child array or a double-freed node shows
     * up here rather than in a production parse. */
    for (round = 0; round < 30; round++) {
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
        const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, 0u);

        ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(rule), 3u);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    }

    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(we_enabled);
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_parse_value(void);

void robo_test_register_css_parse_value(void)
{
    robo_test_register("css.parse.value.component_vocabulary",
                       test_css_parse_value_component_vocabulary);
    robo_test_register("css.parse.value.tokens_keep_their_ranges",
                       test_css_parse_value_tokens_keep_their_ranges);
    robo_test_register("css.parse.value.functions", test_css_parse_value_functions);
    robo_test_register("css.parse.value.nested_functions",
                       test_css_parse_value_nested_functions);
    robo_test_register("css.parse.value.blocks", test_css_parse_value_blocks);
    robo_test_register("css.parse.value.blocks_match_their_close",
                       test_css_parse_value_blocks_record_their_matching_close);
    robo_test_register("css.parse.value.nesting_bound",
                       test_css_parse_value_nesting_bound);
    robo_test_register("css.parse.value.strings", test_css_parse_value_strings);
    robo_test_register("css.parse.value.url_tokens", test_css_parse_value_url_tokens);
    robo_test_register("css.parse.value.url_quoted_is_a_function",
                       test_css_parse_value_url_quoted_is_a_function);
    robo_test_register("css.parse.value.lifetime", test_css_parse_value_lifetime);
}
