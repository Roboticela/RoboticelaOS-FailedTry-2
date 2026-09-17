/*
 * RoboCSS computed styles (Phase 9.8) — css.computed.*
 *
 * The normaliser's own suite proves that `10px` becomes a length. This one
 * proves the *pipeline*: that the value which reaches the normaliser for a
 * property is the one the cascade and inheritance settled on, that a child
 * inherits its parent's **computed** value rather than a recomputation of
 * something that looked like it, and that everything the build cannot settle
 * comes back marked rather than guessed.
 *
 * Three shapes get the most attention because a wrong implementation still
 * produces a plausible style:
 *
 *   The parent's computed value. `div { font-size: 1in } p { font-size: inherit }`
 *   gives the paragraph `96px` — the parent's computed length, not its `1in` —
 *   and a style that recomputed the inherited `1in` would pass a naive equality
 *   test only because both spell the same length.
 *
 *   A property nothing declared. `width` has *no entry* on an element that never
 *   mentions it, and the initial value is reachable through the query door
 *   instead — so "not inherited" is a statement about a property that does not
 *   appear, not about one whose value happens to be `auto`.
 *
 *   `currentColor` and `var()`. Each is carried and flagged, and the flag is
 *   what keeps a consumer from reading a reference as a colour or a custom
 *   property reference as an ordinary token list.
 */

#include "RoboCssTestSupport.h"

/** Compute @p ref's style through the real context door, asserting success. */
static void css_compute_of(css_inherit_fixture_t *fixture,
                           robo_css_element_ref_t ref,
                           robo_css_computed_style_t *out)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(fixture->cascade.context, ref,
                                                          out, &error, NULL),
                      ROBO_OK);
    /* A clean document produces no CSS problem, which is what makes the
     * expectations below about the values rather than about the parse. */
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
}

/** Assert the query door answers @p name with @p expected and @p from_initial. */
static void css_compute_query(css_inherit_fixture_t *fixture,
                              const robo_css_computed_style_t *style,
                              const char *name,
                              const char *expected,
                              int from_initial)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    size_t length = 0u;
    int initial = -1;

    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          fixture->cascade.context, style, robo_css_slice_from_text(name),
                          &value, &initial, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(initial, from_initial);
    text[0] = '\0';
    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, sizeof(text), &length,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(text, expected);
    robo_css_computed_value_dispose(&value);
}

/* ------------------------------------------------------------------ */
/* One element, declared values                                       */
/* ------------------------------------------------------------------ */

static void test_css_computed_declared(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: #336699; width: 50%; height: 2em; display: block }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    css_compute_of(&fixture, div, style);

    css_computed_is(style, "color", "#336699");
    css_computed_flags_are(style, "color", "declared");
    /* A percentage stays a percentage: only layout knows what it is a
     * percentage *of*, and turning it into pixels here would be a made-up
     * number. */
    css_computed_is(style, "width", "50%");
    /* A relative length keeps its unit for the same reason. */
    css_computed_is(style, "height", "2em");
    css_computed_is(style, "display", "block");
    css_computed_flags_are(style, "display", "declared");

    /* The specified value travels with the computed one, so a caller holding
     * one half of "specified vs computed" can see the other. */
    ROBO_CHECK(robo_css_computed_style_specified_by_name(
                   style, robo_css_slice_from_text("color")) != NULL);
    ROBO_CHECK(robo_css_computed_style_find(
                   style, robo_css_slice_from_text("COLOR")) != (size_t)-1);
    ROBO_CHECK(robo_css_computed_style_find(
                   style, robo_css_slice_from_text("nope")) == (size_t)-1);

    /* `width` is declared; `padding` is not, and is *absent* rather than
     * defaulted — the property has no entry because the element said nothing
     * about it, which is a different fact from a defaulted value. */
    css_computed_is(style, "padding", "<absent>");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(style), 0);
    ROBO_CHECK_EQ_INT((int)robo_css_computed_style_incomplete_count(style), 0);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_is_truncated(style), 0);

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Inheritance, through the computed value                            */
/* ------------------------------------------------------------------ */

static void test_css_computed_inherits_computed_value(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent = NULL;
    robo_css_computed_style_t *child = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    /* The parent's declaration is in `in`; the child inherits *pixels*. */
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red; font-size: 1in }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&child, NULL), ROBO_OK);

    css_compute_of(&fixture, div, parent);
    css_compute_of(&fixture, paragraph, child);

    css_computed_is(parent, "font-size", "96px");
    css_computed_is(parent, "color", "#ff0000");
    /* The child was given nothing, so it inherits — and what it inherits is the
     * parent's *computed* value. The assertion is `96px`, which the parent
     * produced from `1in`; a style that carried `1in` down would fail it. */
    css_computed_is(child, "font-size", "96px");
    css_computed_is(child, "color", "#ff0000");
    css_computed_flags_are(child, "color", "inherited");
    css_computed_flags_are(child, "font-size", "inherited");

    /* An inherited value is a copy: the parent's style can go away without
     * touching the child's. */
    robo_css_computed_style_destroy(parent);
    parent = NULL;
    css_computed_is(child, "color", "#ff0000");

    robo_css_computed_style_destroy(child);
    css_inherit_end(&fixture);
}

static void test_css_computed_default_then_inherited(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *root = NULL;
    robo_css_computed_style_t *child = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    /* Nothing is declared anywhere: the document is a tree of defaults, and the
     * point is that a *default* is an answer. */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&root, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&child, NULL), ROBO_OK);

    css_compute_of(&fixture, div, root);
    css_compute_of(&fixture, paragraph, child);

    /* The whole inherited-by-default family is present on the root, each at the
     * registry's initial value, each normalised: `black` is a colour, `serif` is
     * a family list, `medium` is a keyword, and `text-indent`'s `0` is the one
     * bare number CSS lets a length be. */
    ROBO_CHECK_EQ_INT((int)robo_css_computed_style_count(root), 16);
    css_computed_is(root, "color", "#000000");
    css_computed_flags_are(root, "color", "default");
    css_computed_is(root, "font-family", "serif");
    css_computed_is(root, "font-size", "medium");
    css_computed_is(root, "font-weight", "normal");
    css_computed_is(root, "line-height", "normal");
    css_computed_is(root, "text-align", "start");
    css_computed_is(root, "text-indent", "0px");
    css_computed_is(root, "visibility", "visible");
    css_computed_is(root, "white-space", "normal");

    /* The child crosses the family too, and each entry says both things that are
     * true about it: the parent supplied the value and the value is a default. */
    css_computed_is(child, "color", "#000000");
    css_computed_flags_are(child, "color", "inherited+default");
    css_computed_is(child, "font-family", "serif");
    css_computed_is(child, "text-indent", "0px");

    robo_css_computed_style_destroy(child);
    robo_css_computed_style_destroy(root);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Non-inherited properties                                           */
/* ------------------------------------------------------------------ */

static void test_css_computed_not_inherited(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *child = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { width: 500px; margin: 0 auto; background: blue }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&child, NULL), ROBO_OK);

    css_compute_of(&fixture, paragraph, child);

    /* None of the parent's non-inherited properties reach the child — not as a
     * missing value and not as a copied one. */
    css_computed_is(child, "width", "<absent>");
    css_computed_is(child, "margin", "<absent>");
    css_computed_is(child, "background", "<absent>");

    /* And the initial value is reachable through the query door, which is where
     * a non-inherited property's "nothing declared it" answer lives. */
    css_compute_query(&fixture, child, "width", "auto", 1);
    css_compute_query(&fixture, child, "display", "inline", 1);
    css_compute_query(&fixture, child, "position", "static", 1);
    /* A custom property has no initial value in CSS, so there is no answer and
     * the door says so rather than inventing one. */
    {
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                              fixture.cascade.context, child,
                              robo_css_slice_from_text("--x"), &value, NULL, NULL),
                          ROBO_NOT_FOUND);
    }
    /* Neither has an unknown property: nothing can say what it starts at. */
    {
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                              fixture.cascade.context, child,
                              robo_css_slice_from_text("totally-unknown"), &value, NULL,
                              NULL),
                          ROBO_NOT_FOUND);
    }

    robo_css_computed_style_destroy(child);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The CSS-wide keywords                                              */
/* ------------------------------------------------------------------ */

static void test_css_computed_wide_keywords(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent = NULL;
    robo_css_computed_style_t *initial_style = NULL;
    robo_css_computed_style_t *inherit_style = NULL;
    robo_css_computed_style_t *unset_style = NULL;
    robo_css_computed_style_t *revert_style = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t initial_child;
    robo_css_element_ref_t inherit_child;
    robo_css_element_ref_t unset_child;
    robo_css_element_ref_t revert_child;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    initial_child = css_host_add(css_inherit_host(&fixture), "p", "a", NULL);
    inherit_child = css_host_add(css_inherit_host(&fixture), "p", "b", NULL);
    unset_child = css_host_add(css_inherit_host(&fixture), "p", "c", NULL);
    revert_child = css_host_add(css_inherit_host(&fixture), "p", "d", NULL);
    css_host_set_parent(css_inherit_host(&fixture), initial_child, div);
    css_host_set_parent(css_inherit_host(&fixture), inherit_child, div);
    css_host_set_parent(css_inherit_host(&fixture), unset_child, div);
    css_host_set_parent(css_inherit_host(&fixture), revert_child, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red; width: 500px }"
                      "#a { color: initial; width: initial }"
                      "#b { color: inherit; width: inherit }"
                      "#c { color: unset; width: unset }"
                      "#d { color: revert; width: revert }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&initial_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&inherit_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&unset_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&revert_style, NULL), ROBO_OK);

    css_compute_of(&fixture, div, parent);
    css_compute_of(&fixture, initial_child, initial_style);
    css_compute_of(&fixture, inherit_child, inherit_style);
    css_compute_of(&fixture, unset_child, unset_style);
    css_compute_of(&fixture, revert_child, revert_style);

    /* `initial` on an inherited property resets it rather than inheriting: the
     * paragraph is black, not red. */
    css_computed_is(initial_style, "color", "#000000");
    css_computed_flags_are(initial_style, "color", "default");
    /* `initial` on a non-inherited property is the property's own start value. */
    css_computed_is(initial_style, "width", "auto");
    css_computed_flags_are(initial_style, "width", "default");

    /* `inherit` on either kind takes the parent's computed value. */
    css_computed_is(inherit_style, "color", "#ff0000");
    css_computed_flags_are(inherit_style, "color", "inherited");
    css_computed_is(inherit_style, "width", "500px");
    css_computed_flags_are(inherit_style, "width", "inherited");

    /* `unset` is `inherit` for a property that inherits and `initial` for one
     * that does not — the same declaration, two different answers, which is the
     * whole point of the keyword. */
    css_computed_is(unset_style, "color", "#ff0000");
    css_computed_flags_are(unset_style, "color", "inherited");
    css_computed_is(unset_style, "width", "auto");
    css_computed_flags_are(unset_style, "width", "default");

    /* `revert` is recognised, carried and *reported*: obeying it means re-running
     * the cascade with an origin removed, and half-obeying it would be wrong in
     * exactly the cases it exists for. */
    css_computed_is(revert_style, "color", "revert");
    css_computed_flags_are(revert_style, "color", "deferred+unresolved");
    ROBO_CHECK_EQ_INT((int)robo_css_computed_style_incomplete_count(revert_style), 2);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(revert_style), 0);

    robo_css_computed_style_destroy(revert_style);
    robo_css_computed_style_destroy(unset_style);
    robo_css_computed_style_destroy(inherit_style);
    robo_css_computed_style_destroy(initial_style);
    robo_css_computed_style_destroy(parent);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Unresolved and invalid values                                      */
/* ------------------------------------------------------------------ */

static void test_css_computed_unresolved_and_invalid(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { width: calc(100% - 20px); background: var(--bg, red);"
                      " color: nonsense; display: block }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    css_compute_of(&fixture, div, style);

    /* A value only layout can reduce is carried with its tokens, flagged as
     * needing a later stage — not rejected, and not reduced to a guess. */
    css_computed_is(style, "width", "calc(100% - 20px)");
    css_computed_flags_are(style, "width", "declared+unresolved");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_computed_by_name(
                          style, robo_css_slice_from_text("width"))->kind,
                      ROBO_CSS_COMPUTED_VALUE_PRESERVED);

    /* A custom property reference is preserved with its fallback intact. */
    css_computed_is(style, "background", "var(--bg, red)");
    css_computed_flags_are(style, "background", "declared+unresolved");

    /* A value that means nothing for its property is reported *invalid* rather
     * than dropped: the declaration was there and was wrong, and a consumer can
     * see that instead of finding the property silently absent. */
    css_computed_flags_are(style, "color", "declared+invalid");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(style), 1);
    ROBO_CHECK_EQ_INT((int)robo_css_computed_style_incomplete_count(style), 3);
    /* The invalid entry does not corrupt its neighbours: `display` is fine. */
    css_computed_is(style, "display", "block");
    css_computed_flags_are(style, "display", "declared");

    /* `currentColor` is the same kind of fact: legal, carried, and *not*
     * resolved against a default. */
    {
        robo_css_computed_style_t *second = NULL;

        ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&second, NULL), ROBO_OK);
        css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: currentColor }");
        css_compute_of(&fixture, div, second);
        css_computed_is(second, "color", "currentColor");
        css_computed_flags_are(second, "color", "declared+unresolved");
        robo_css_computed_style_destroy(second);
    }

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Custom properties and inline styles                                */
/* ------------------------------------------------------------------ */

static void test_css_computed_custom_properties(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent = NULL;
    robo_css_computed_style_t *child = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { --theme-color: 10px; --other: \"a string\" }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&child, NULL), ROBO_OK);

    css_compute_of(&fixture, div, parent);
    css_compute_of(&fixture, paragraph, child);

    /* A custom property keeps its tokens: `10px` is not turned into a length,
     * because nothing has said what the property *is*. */
    css_computed_is(parent, "--theme-color", "10px");
    css_computed_flags_are(parent, "--theme-color", "declared+custom");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_computed_by_name(
                          parent, robo_css_slice_from_text("--theme-color"))->kind,
                      ROBO_CSS_COMPUTED_VALUE_PRESERVED);
    css_computed_is(parent, "--other", "\"a string\"");

    /* Custom properties inherit by default (CSS says so, and only the name can
     * say it: no registry entry exists for one). */
    css_computed_is(child, "--theme-color", "10px");
    css_computed_flags_are(child, "--theme-color", "inherited+custom");

    /* And the name comparison is byte-exact for them, which is the rule that
     * keeps `--Theme` from finding `--theme`. */
    ROBO_CHECK(robo_css_computed_style_find(
                   child, robo_css_slice_from_text("--theme-color")) != (size_t)-1);
    ROBO_CHECK(robo_css_computed_style_find(
                   child, robo_css_slice_from_text("--THEME-COLOR")) == (size_t)-1);

    robo_css_computed_style_destroy(child);
    robo_css_computed_style_destroy(parent);
    css_inherit_end(&fixture);
}

/**
 * An inline-style stub that answers for *one* element.
 *
 * The shared stub hands the same text to whatever is asked, which is the right
 * shape for testing that the attribute is read at all. This test needs the other
 * thing — an attribute on the parent and none on the child — because otherwise
 * the child would declare its own colour and the assertion would no longer be
 * about inheritance.
 */
typedef struct css_inline_target {
    robo_css_element_ref_t ref;
    const char *text;
    int calls;
} css_inline_target_t;

static robo_result_t css_inline_target_load(void *userdata,
                                            robo_css_element_ref_t element,
                                            robo_css_slice_t *out_text)
{
    css_inline_target_t *target = (css_inline_target_t *)userdata;

    target->calls++;
    if (element != target->ref) {
        return ROBO_NOT_FOUND;
    }
    *out_text = robo_css_slice_from_text(target->text);
    return ROBO_OK;
}

static void test_css_computed_inline_style(void)
{
    css_inherit_fixture_t fixture;
    css_inline_target_t target;
    robo_css_computed_style_t *parent = NULL;
    robo_css_computed_style_t *child = NULL;
    robo_css_computed_style_t *override = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;
    robo_css_element_ref_t own;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    own = css_host_add(css_inherit_host(&fixture), "p", "third", NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_set_parent(css_inherit_host(&fixture), own, div);
    css_host_seal(css_inherit_host(&fixture));

    /* The stub is standing in for the DOM: RoboCSS asks it for the `style`
     * attribute's *text* and parses it through the same grammar a rule uses, so
     * an inline declaration is not a second dialect of CSS. */
    target.ref = div;
    target.text = "color: blue; padding: 1in";
    target.calls = 0;
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_inline_style(
                          fixture.cascade.context, css_inline_target_load, &target,
                          NULL),
                      ROBO_OK);
    /* A rule on the second paragraph, so the child that has its own value can be
     * told apart from the one that has none. */
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "#third { color: green }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&child, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&override, NULL), ROBO_OK);

    css_compute_of(&fixture, div, parent);
    css_compute_of(&fixture, paragraph, child);
    css_compute_of(&fixture, own, override);

    css_computed_is(parent, "color", "#0000ff");
    css_computed_flags_are(parent, "color", "declared");
    /* A shorthand keeps its tokens: `padding: 1in` is not four longhands, and
     * expanding it would be inventing a shorthand engine. */
    css_computed_is(parent, "padding", "1in");
    /* The first paragraph has no `style` attribute and inherits the resolved
     * inline colour. The second one's rule overrides it, because inheritance
     * never competes with a declaration on the element itself. */
    css_computed_is(child, "color", "#0000ff");
    css_computed_flags_are(child, "color", "inherited");
    css_computed_is(override, "color", "#008000");
    css_computed_flags_are(override, "color", "declared");
    /* The stub was asked once per element, which is what makes the inheritance
     * above a statement about an attribute rather than about a fallback. */
    ROBO_CHECK_EQ_INT(target.calls >= 3, 1);

    robo_css_computed_style_destroy(override);
    robo_css_computed_style_destroy(child);
    robo_css_computed_style_destroy(parent);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The ancestor walk                                                  */
/* ------------------------------------------------------------------ */

static void test_css_computed_chain(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t section;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    section = css_host_add(css_inherit_host(&fixture), "section", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), section, div);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, section);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red } section { color: blue }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    /* The whole chain is computed top-down, and the paragraph gets the value of
     * its *immediate* parent — not the nearest ancestor that happened to
     * declare something. */
    css_compute_of(&fixture, paragraph, style);
    css_computed_is(style, "color", "#0000ff");
    css_computed_flags_are(style, "color", "inherited");
    /* `red` on the grandparent is nowhere in the answer: the immediate parent's
     * computed value is what an inheritance chain propagates. */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style) > 0u, 1);

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * A DOM that reports a cycle among parents must not walk forever.
 *
 * The host double can be made to lie about paternity, and the walk cannot detect
 * the lie — it only ever asks for one element's parent. What it can do is bound
 * the walk, and the point of this test is that a capped chain is *reported*: a
 * style whose top is not a root is a lower bound, and a caller that could not
 * tell would read inherited values as certain.
 */
static void test_css_computed_cyclic_chain_is_truncated(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t ring[4];
    size_t index;

    css_fixture();
    css_inherit_begin(&fixture);
    for (index = 0u; index < 4u; index++) {
        ring[index] = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    }
    for (index = 0u; index < 4u; index++) {
        css_host_set_parent(css_inherit_host(&fixture), ring[index],
                            ring[(index + 1u) % 4u]);
    }
    css_host_seal(css_inherit_host(&fixture));
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                          fixture.cascade.context, ring[0], style, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_is_truncated(style), 1);
    /* No crash, no hang, and the defaults are still there. */
    css_computed_is(style, "color", "#000000");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism and arguments                                          */
/* ------------------------------------------------------------------ */

static void test_css_computed_determinism(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *first = NULL;
    robo_css_computed_style_t *second = NULL;
    robo_css_element_ref_t div;
    size_t index;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "app", "panel wide");
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red; width: 10px; display: block }"
                      ".panel { color: blue; height: 50% }"
                      "#app { padding: 1px }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&first, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&second, NULL), ROBO_OK);

    css_compute_of(&fixture, div, first);
    css_compute_of(&fixture, div, second);

    /* Same inputs, same answer — entries, order, values and flags alike. Not a
     * comparison of re-derived answers but of the two sequences. */
    ROBO_CHECK_EQ_INT((int)robo_css_computed_style_count(first),
                      (int)robo_css_computed_style_count(second));
    for (index = 0u; index < robo_css_computed_style_count(first); index++) {
        robo_css_property_id_t property_a = ROBO_CSS_PROPERTY_ID_NONE;
        robo_css_property_id_t property_b = ROBO_CSS_PROPERTY_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_css_computed_style_property_at(first, index, &property_a,
                                                             NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_style_property_at(second, index, &property_b,
                                                             NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)property_a, (int)property_b);
        ROBO_CHECK_EQ_INT((int)robo_css_computed_style_flags_at(first, index),
                          (int)robo_css_computed_style_flags_at(second, index));
        ROBO_CHECK_EQ_INT(robo_css_computed_value_equals(
                              robo_css_computed_style_computed_at(first, index),
                              robo_css_computed_style_computed_at(second, index)),
                          1);
    }
    /* Recomputing into a style that already holds one replaces rather than
     * appends, which is what makes a second computation on a reused object the
     * same answer as the first. */
    css_compute_of(&fixture, div, first);
    ROBO_CHECK_EQ_INT((int)robo_css_computed_style_count(first),
                      (int)robo_css_computed_style_count(second));

    /* The cascade decided the winners before any of this: `color` is the
     * `.panel` blue and `height` is its 50%. */
    css_computed_is(first, "color", "#0000ff");
    css_computed_is(first, "height", "50%");
    css_computed_is(first, "padding", "1px");

    robo_css_computed_style_destroy(second);
    robo_css_computed_style_destroy(first);
    css_inherit_end(&fixture);
}

static void test_css_computed_arguments(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(NULL, div, style, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(fixture.cascade.context, div,
                                                           NULL, &error, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    /* "No element" is not an element, and resolving one is not an answer. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                          fixture.cascade.context, ROBO_CSS_ELEMENT_REF_NONE, style,
                          &error, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 0u);

    /* The error record is optional, which every other door in the subsystem
     * already promises. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(fixture.cascade.context, div,
                                                           style, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK((int)robo_css_computed_style_count(style) > 0);

    /* The query door's own arguments. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          fixture.cascade.context, style, robo_css_slice_empty(), NULL,
                          NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          NULL, style, robo_css_slice_from_text("color"), NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                              fixture.cascade.context, NULL,
                              robo_css_slice_from_text("nope"), &value, NULL, NULL),
                          ROBO_NOT_FOUND);
    }
    /* A NULL style means "nothing was computed", and every known property still
     * answers with its initial value — which is a different fact from having no
     * answer at all. */
    css_compute_query(&fixture, NULL, "color", "#000000", 1);

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * A document with no stylesheets at all.
 *
 * The cascade is empty, inheritance has nothing to inherit, and the computed
 * style is still a complete answer: every inherited-by-default property at its
 * initial value and nothing else. An element with no declarations is not an
 * error, and a style that came back empty would be.
 */
static void test_css_computed_no_stylesheets(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    css_compute_of(&fixture, div, style);
    ROBO_CHECK_EQ_INT((int)robo_css_computed_style_count(style), 16);
    css_computed_is(style, "color", "#000000");
    css_computed_is(style, "display", "<absent>");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_is_truncated(style), 0);

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_computed(void);

void robo_test_register_css_computed(void)
{
    robo_test_register("css.computed.declared", test_css_computed_declared);
    robo_test_register("css.computed.inherits_computed_value",
                       test_css_computed_inherits_computed_value);
    robo_test_register("css.computed.default_then_inherited",
                       test_css_computed_default_then_inherited);
    robo_test_register("css.computed.not_inherited", test_css_computed_not_inherited);
    robo_test_register("css.computed.wide_keywords", test_css_computed_wide_keywords);
    robo_test_register("css.computed.unresolved_and_invalid",
                       test_css_computed_unresolved_and_invalid);
    robo_test_register("css.computed.custom_properties",
                       test_css_computed_custom_properties);
    robo_test_register("css.computed.inline_style", test_css_computed_inline_style);
    robo_test_register("css.computed.chain", test_css_computed_chain);
    robo_test_register("css.computed.cyclic_chain_is_truncated",
                       test_css_computed_cyclic_chain_is_truncated);
    robo_test_register("css.computed.determinism", test_css_computed_determinism);
    robo_test_register("css.computed.arguments", test_css_computed_arguments);
    robo_test_register("css.computed.no_stylesheets", test_css_computed_no_stylesheets);
}
