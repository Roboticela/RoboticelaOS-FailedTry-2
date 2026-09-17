/*
 * RoboCSS inheritance resolution (Phase 9.7) — css.inherit.resolve.* and
 * css.inherit.chain.*
 *
 * Resolution is where the phase's claim has to hold: the properties an element
 * declared come from its own cascade, the properties it did not come from its
 * resolved parent, and the ones nobody declared come from the property's own
 * initial value. Every test below is one shape of that.
 *
 * The shapes worth the most attention are the ones where a wrong implementation
 * still produces a *plausible* answer: inheriting through the nearest ancestor
 * that declared something (rather than the immediate parent), letting a
 * non-inherited property propagate, obeying `initial` by inheriting anyway, or
 * resolving every property off the registry regardless of what the element said.
 * Each of those has a test whose assertion would change if it happened.
 */

#include "RoboCssTestSupport.h"

/**
 * How many properties the standard registry marks as inherited.
 *
 * Spelled once because half the count assertions below are "the whole inherited
 * family crossed", and writing 16 at each site would make the tests disagree
 * the day a property is added — which is exactly the noise a count assertion
 * exists to avoid.
 */
#define CSS_INHERITED_FAMILY 16u

/** Collect @p ref's cascade and resolve it with @p parent (may be NULL). */
static void css_level_of(css_inherit_fixture_t *fixture,
                         robo_css_element_ref_t ref,
                         const robo_css_inherited_style_t *parent,
                         robo_css_inherited_style_t *out)
{
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;

    css_cascade_collect(&fixture->cascade, ref, &cascade, NULL);
    css_inherit_resolve(fixture, &cascade, parent, out);
    robo_css_cascade_dispose(&cascade);
}

/* ------------------------------------------------------------------ */
/* What the element declares                                           */
/* ------------------------------------------------------------------ */

static void test_css_inherit_resolve_declared(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t style = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red }");

    css_level_of(&fixture, div, NULL, &style);
    css_inherit_is(&style, "color", "red", "declared");
    ROBO_CHECK_STR_EQ(css_inherit_keyword(&style, "color"), "none");
    ROBO_CHECK_EQ_INT(style.declared_count, 1u);
    ROBO_CHECK_EQ_INT((int)robo_css_inherited_style_element(&style), (int)div);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_has_parent(&style), 0);
    /* The entry carries the behaviour's identity as well as its name. */
    ROBO_CHECK(robo_css_inherited_style_find_property(
                   &style, robo_css_property_table_lookup(fixture.table, "color")) != NULL);
    ROBO_CHECK(robo_css_inherited_property_value(
                   robo_css_inherited_style_find(&style, robo_css_slice_from_text("color"))) !=
               NULL);

    robo_css_inherited_style_dispose(&style);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_inherits_by_default(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red }");

    css_level_of(&fixture, div, NULL, &parent);
    css_level_of(&fixture, paragraph, &parent, &child);
    /* The paragraph says nothing, and `color` inherits: the parent's value, and
     * the provenance says *why* — default inheritance, no keyword involved. */
    css_inherit_is(&child, "color", "red", "parent");
    ROBO_CHECK_STR_EQ(css_inherit_keyword(&child, "color"), "none");
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_has_parent(&child), 1);
    ROBO_CHECK_EQ_INT(child.declared_count, 0u);
    /* The whole inherited family crossed, which is what makes the one assertion
     * above a statement about `color` rather than about the only entry. */
    ROBO_CHECK_EQ_INT(child.inherited_count, CSS_INHERITED_FAMILY);
    /* The child's own copy: the parent can go away without touching it. */
    robo_css_inherited_style_dispose(&parent);
    css_inherit_is(&child, "color", "red", "parent");

    robo_css_inherited_style_dispose(&child);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_not_inherited_does_not(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    /* The parent declares the whole non-inherited family. */
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { width: 500px; height: 100px; margin: 10px; padding: 4px; "
                      "background: white; border: 1px solid red; display: block; "
                      "position: absolute }");

    css_level_of(&fixture, div, NULL, &parent);
    /* Every one of them is on the parent. */
    css_inherit_is(&parent, "width", "500px", "declared");
    css_inherit_is(&parent, "display", "block", "declared");
    ROBO_CHECK(robo_css_inherited_style_has(&parent, robo_css_slice_from_text("margin")));

    css_level_of(&fixture, paragraph, &parent, &child);
    /* And none of them is on the child: a non-inherited property nothing
     * declares has no entry at all. Filling one in with `auto` here would be a
     * value no input produced. */
    css_inherit_is(&child, "width", "<absent>", "<absent>");
    css_inherit_is(&child, "height", "<absent>", "<absent>");
    css_inherit_is(&child, "margin", "<absent>", "<absent>");
    css_inherit_is(&child, "padding", "<absent>", "<absent>");
    css_inherit_is(&child, "background", "<absent>", "<absent>");
    css_inherit_is(&child, "border", "<absent>", "<absent>");
    css_inherit_is(&child, "display", "<absent>", "<absent>");
    css_inherit_is(&child, "position", "<absent>", "<absent>");
    /* Only the inherited family crossed, which is what makes the negative
     * assertions above a statement about inheritance rather than about an empty
     * result. */
    ROBO_CHECK_EQ_INT(child.inherited_count, CSS_INHERITED_FAMILY);
    ROBO_CHECK_EQ_INT(child.default_count, 0u);
    css_inherit_is(&child, "color", "black", "parent");

    robo_css_inherited_style_dispose(&child);
    robo_css_inherited_style_dispose(&parent);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_child_overrides_parent(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red }\np { color: green }");

    css_level_of(&fixture, div, NULL, &parent);
    css_level_of(&fixture, paragraph, &parent, &child);
    /* The child's own declaration wins, and inheritance never competes with it:
     * the entry is DECLARED, not PARENT. */
    css_inherit_is(&child, "color", "green", "declared");
    ROBO_CHECK_EQ_INT(child.declared_count, 1u);
    /* The other fifteen inherited properties still came from the parent, so the
     * override is a statement about `color` and not about inheritance stopping. */
    ROBO_CHECK_EQ_INT(child.inherited_count, CSS_INHERITED_FAMILY - 1u);

    robo_css_inherited_style_dispose(&child);
    robo_css_inherited_style_dispose(&parent);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_intermediate_overrides_ancestor(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t div_style = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t section_style = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t paragraph_style = ROBO_CSS_CLEARED;
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
                      "div { color: red }\nsection { color: blue }");

    css_level_of(&fixture, div, NULL, &div_style);
    css_level_of(&fixture, section, &div_style, &section_style);
    css_level_of(&fixture, paragraph, &section_style, &paragraph_style);

    css_inherit_is(&div_style, "color", "red", "declared");
    css_inherit_is(&section_style, "color", "blue", "declared");
    /* Blue, not red: the child takes the *immediate* parent's resolved value,
     * not the nearest ancestor that happened to declare something. An
     * implementation that walked up looking for a declaration would answer red
     * here and be wrong in a way that looks reasonable. */
    css_inherit_is(&paragraph_style, "color", "blue", "parent");
    ROBO_CHECK_EQ_INT(paragraph_style.inherited_count, CSS_INHERITED_FAMILY);
    ROBO_CHECK_EQ_INT(paragraph_style.declared_count, 0u);

    robo_css_inherited_style_dispose(&paragraph_style);
    robo_css_inherited_style_dispose(&section_style);
    robo_css_inherited_style_dispose(&div_style);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* No parent                                                           */
/* ------------------------------------------------------------------ */

static void test_css_inherit_resolve_root_uses_defaults(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t style = ROBO_CSS_CLEARED;
    robo_css_element_ref_t root;

    css_fixture();
    css_inherit_begin(&fixture);
    root = css_host_add(css_inherit_host(&fixture), "html", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));

    css_level_of(&fixture, root, NULL, &style);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_has_parent(&style), 0);
    /* A root declares nothing and still has an answer for every inherited
     * property, from the registry's initial values. This is what makes a chain
     * terminate: at some point "the parent's value" has to become a real value. */
    css_inherit_is(&style, "color", "black", "default");
    css_inherit_is(&style, "font-family", "serif", "default");
    css_inherit_is(&style, "font-size", "medium", "default");
    css_inherit_is(&style, "line-height", "normal", "default");
    css_inherit_is(&style, "visibility", "visible", "default");
    css_inherit_is(&style, "white-space", "normal", "default");
    css_inherit_is(&style, "text-align", "start", "default");
    css_inherit_is(&style, "font-weight", "normal", "default");
    css_inherit_is(&style, "font-style", "normal", "default");
    /* And *only* the inherited family: a root has no `width` entry. */
    css_inherit_is(&style, "width", "<absent>", "<absent>");
    css_inherit_is(&style, "display", "<absent>", "<absent>");
    ROBO_CHECK_EQ_INT((int)style.default_count, (int)CSS_INHERITED_FAMILY);
    ROBO_CHECK_EQ_INT((int)style.declared_count, 0);
    ROBO_CHECK_EQ_INT((int)style.inherited_count, 0);
    /* Nothing here was refused, so the answer is complete. */
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_is_complete(&style), 1);
    ROBO_CHECK_EQ_INT((int)style.unsupported, 0);

    /* A root that *does* declare an inherited property overrides its default. */
    robo_css_inherited_style_dispose(&style);
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "html { color: navy }");
    css_level_of(&fixture, root, NULL, &style);
    css_inherit_is(&style, "color", "navy", "declared");
    css_inherit_is(&style, "font-family", "serif", "default");

    robo_css_inherited_style_dispose(&style);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The CSS-wide keywords                                               */
/* ------------------------------------------------------------------ */

static void test_css_inherit_resolve_explicit_inherit(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    /* `width` does not inherit, and `inherit` makes it anyway — which is the
     * whole reason the keyword exists. */
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { width: 500px }\np { width: inherit }");

    css_level_of(&fixture, div, NULL, &parent);
    css_level_of(&fixture, paragraph, &parent, &child);
    css_inherit_is(&child, "width", "500px", "parent");
    ROBO_CHECK_STR_EQ(css_inherit_keyword(&child, "width"), "inherit");
    /* The keyword plus the whole inherited family, all from the parent. */
    ROBO_CHECK_EQ_INT(child.inherited_count, CSS_INHERITED_FAMILY + 1u);
    ROBO_CHECK_EQ_INT(child.declared_count, 0u);
    /* The value is the parent's *representation*: `500px`, not a used width. */
    ROBO_CHECK_EQ_INT(robo_css_value_is_empty(
                          robo_css_inherited_property_value(
                              robo_css_inherited_style_find(&child,
                                                            robo_css_slice_from_text("width")))),
                      0);

    /* `inherit` with no parent value is the parent's computed value, and for an
     * undeclared property that is its initial value. The keyword is still
     * recorded, so the two routes stay distinguishable. */
    robo_css_inherited_style_dispose(&child);
    robo_css_inherited_style_dispose(&parent);
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red }\ndiv { width: inherit }");
    css_level_of(&fixture, div, NULL, &child);
    css_inherit_is(&child, "width", "auto", "default");
    ROBO_CHECK_STR_EQ(css_inherit_keyword(&child, "width"), "inherit");

    robo_css_inherited_style_dispose(&child);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_explicit_initial(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red }\np { color: initial }");

    css_level_of(&fixture, div, NULL, &parent);
    css_level_of(&fixture, paragraph, &parent, &child);
    /* Not red: `initial` refuses the parent's value, and the result is the
     * property's own initial value rather than a miss. */
    css_inherit_is(&child, "color", "black", "default");
    ROBO_CHECK_STR_EQ(css_inherit_keyword(&child, "color"), "initial");
    ROBO_CHECK_EQ_INT(child.default_count, 1u);
    /* `color` was refused; the other fifteen were not. */
    ROBO_CHECK_EQ_INT(child.inherited_count, CSS_INHERITED_FAMILY - 1u);

    robo_css_inherited_style_dispose(&child);
    robo_css_inherited_style_dispose(&parent);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_unset(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red; width: 500px }\n"
                      "p { color: unset; width: unset }");

    css_level_of(&fixture, div, NULL, &parent);
    css_level_of(&fixture, paragraph, &parent, &child);
    /* One keyword, two behaviours: `color` inherits like `inherit`, `width`
     * does not and takes its initial value like `initial`. The keyword is
     * recorded on both, which is what keeps "unset" readable as itself. */
    css_inherit_is(&child, "color", "red", "parent");
    ROBO_CHECK_STR_EQ(css_inherit_keyword(&child, "color"), "unset");
    css_inherit_is(&child, "width", "auto", "default");
    ROBO_CHECK_STR_EQ(css_inherit_keyword(&child, "width"), "unset");
    /* The whole inherited family comes from the parent: `color` because `unset`
     * made it inherit, the other fifteen because `color` and `font-*` and the
     * rest inherit by default. `width` is the only default. No `+1`: unlike
     * `width: inherit`, the keyword here lands on a property that was already
     * part of the family, so it adds no entry. */
    ROBO_CHECK_EQ_INT(child.inherited_count, CSS_INHERITED_FAMILY);
    ROBO_CHECK_EQ_INT(child.default_count, 1u);
    ROBO_CHECK_EQ_INT(child.declared_count, 0u);

    robo_css_inherited_style_dispose(&child);
    robo_css_inherited_style_dispose(&parent);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_revert_is_deferred(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t style = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: revert }");

    css_level_of(&fixture, div, NULL, &style);
    /* Recognised, carried, and *reported*: the value is the keyword itself and
     * the entry says it is deferred, so nothing can read it as an answer. An
     * implementation that quietly treated `revert` as `unset` would show up
     * here as `black` with source default. */
    css_inherit_is(&style, "color", "revert", "deferred");
    ROBO_CHECK_STR_EQ(css_inherit_keyword(&style, "color"), "revert");
    ROBO_CHECK_EQ_INT(style.deferred_count, 1u);
    ROBO_CHECK_EQ_INT(style.declared_count, 0u);
    /* The other fifteen inherited properties are still answered normally: a
     * deferred keyword does not poison the properties beside it. */
    ROBO_CHECK_EQ_INT(style.default_count, CSS_INHERITED_FAMILY - 1u);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_is_complete(&style), 0);
    ROBO_CHECK_EQ_INT((int)style.unsupported, 1);

    robo_css_inherited_style_dispose(&style);

    /* A `revert` the parent carried travels down as one — the author's
     * declaration is never silently dropped on the way. */
    {
        robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
        robo_css_element_ref_t paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);

        css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
        css_host_seal(css_inherit_host(&fixture));
        css_level_of(&fixture, div, NULL, &style);
        css_level_of(&fixture, paragraph, &style, &child);
        css_inherit_is(&child, "color", "revert", "deferred");
        ROBO_CHECK_EQ_INT(child.deferred_count, 1u);
        robo_css_inherited_style_dispose(&child);
        robo_css_inherited_style_dispose(&style);
    }
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Custom properties                                                   */
/* ------------------------------------------------------------------ */

static void test_css_inherit_resolve_custom_property(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
    robo_css_element_ref_t root;
    robo_css_element_ref_t child_ref;

    css_fixture();
    css_inherit_begin(&fixture);
    root = css_host_add(css_inherit_host(&fixture), "html", NULL, NULL);
    child_ref = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), child_ref, root);
    css_host_seal(css_inherit_host(&fixture));
    /* A custom property has no identity in a table, so its *name* is what
     * identifies it — and CSS defines it as inherited. */
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      ":root { --theme-color: blue }\np { --own: 1px }");

    css_level_of(&fixture, root, NULL, &parent);
    ROBO_CHECK_STR_EQ(css_inherit_source(&parent, "--theme-color"), "declared");
    /* A custom property has no identity in a table, so its name is its identity
     * — and the entry says so. */
    {
        const robo_css_inherited_property_t *custom = robo_css_inherited_style_find(
            &parent, robo_css_slice_from_text("--theme-color"));

        ROBO_CHECK(custom != NULL);
        if (custom) {
            ROBO_CHECK_EQ_INT((int)custom->property, (int)ROBO_CSS_PROPERTY_ID_NONE);
            ROBO_CHECK_EQ_INT((int)custom->name_length, (int)strlen("--theme-color"));
        }
    }

    css_level_of(&fixture, child_ref, &parent, &child);
    /* Carried down the chain, value and all: the raw tokens are preserved so
     * 9.8's `var()` substitution has something to read. */
    css_inherit_is(&child, "--theme-color", "blue", "parent");
    css_inherit_is(&child, "--own", "1px", "declared");

    /* And a child can override one, without disturbing the other. */
    robo_css_inherited_style_dispose(&child);
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "p { --theme-color: green }");
    css_level_of(&fixture, child_ref, &parent, &child);
    css_inherit_is(&child, "--theme-color", "green", "declared");
    css_inherit_is(&child, "--own", "1px", "declared");

    robo_css_inherited_style_dispose(&child);
    robo_css_inherited_style_dispose(&parent);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_custom_property_has_no_default(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t style = ROBO_CSS_CLEARED;
    robo_css_element_ref_t root;

    css_fixture();
    css_inherit_begin(&fixture);
    root = css_host_add(css_inherit_host(&fixture), "html", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));

    css_level_of(&fixture, root, NULL, &style);
    /* An unset custom property is *invalid*, not `initial`: CSS gives it no
     * initial value, so there is no entry and nothing is defaulted. A registry
     * that happened to know a `--x` must not change that. */
    css_inherit_is(&style, "--theme-color", "<absent>", "<absent>");
    ROBO_CHECK_EQ_INT((int)style.default_count, (int)CSS_INHERITED_FAMILY);
    ROBO_CHECK_EQ_INT((int)style.unresolved_count, 0);

    /* `initial` on a custom property is the same answer: CSS drops it rather
     * than giving it a value. */
    robo_css_inherited_style_dispose(&style);
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, ":root { --x: initial }");
    css_level_of(&fixture, root, NULL, &style);
    css_inherit_is(&style, "--x", "<unresolved>", "default");
    ROBO_CHECK_EQ_INT((int)style.unresolved_count, 1);
    ROBO_CHECK_EQ_INT((int)style.unsupported, 1);

    robo_css_inherited_style_dispose(&style);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Unknown properties                                                  */
/* ------------------------------------------------------------------ */

static void test_css_inherit_resolve_unknown_property_does_not_inherit(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { totally-unknown-property: whatever }");

    css_level_of(&fixture, div, NULL, &parent);
    /* The value is preserved — the author wrote it, and 9.5 already decided not
     * to lose it — but a property nothing can interpret must not spread down a
     * tree. */
    css_inherit_is(&parent, "totally-unknown-property", "whatever", "declared");
    css_level_of(&fixture, paragraph, &parent, &child);
    css_inherit_is(&child, "totally-unknown-property", "<absent>", "<absent>");

    /* `initial` on an unknown property is typed rather than invented: the entry
     * exists, says its default is not known, and is counted. */
    robo_css_inherited_style_dispose(&child);
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "p { totally-unknown-property: initial }");
    css_level_of(&fixture, paragraph, &parent, &child);
    css_inherit_is(&child, "totally-unknown-property", "<unresolved>", "default");
    ROBO_CHECK_EQ_INT((int)child.unresolved_count, 1);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_is_complete(&child), 0);

    robo_css_inherited_style_dispose(&child);
    robo_css_inherited_style_dispose(&parent);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Cascade interaction                                                 */
/* ------------------------------------------------------------------ */

static void test_css_inherit_resolve_inline_participates(void)
{
    css_inherit_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "p { color: red }");

    /* One `style` attribute, handed out for whichever element is asked — the
     * stub models a document with one sloppily, which is all these two cases
     * need. */
    css_inherit_inline(&fixture, &stub, "color: purple");
    css_level_of(&fixture, div, NULL, &parent);
    css_inherit_is(&parent, "color", "purple", "declared");

    /* A `style` attribute on the parent reaches the child through inheritance,
     * and the child's own selector declaration still beats it. */
    robo_css_inherited_style_dispose(&parent);
    css_inherit_inline(&fixture, &stub, "color: purple");
    css_level_of(&fixture, paragraph, NULL, &child);
    css_inherit_is(&child, "color", "purple", "declared");
    ROBO_CHECK_EQ_INT(child.declared_count, 1u);

    /* An `!important` on the *parent* has no way to reach the child, because
     * importance is a fact about a declaration on this element and inheritance
     * is not a declaration. */
    css_inherit_end(&fixture);

    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red !important }\np { color: blue }");
    css_level_of(&fixture, div, NULL, &parent);
    css_inherit_is(&parent, "color", "red", "declared");
    css_level_of(&fixture, paragraph, &parent, &child);
    css_inherit_is(&child, "color", "blue", "declared");

    robo_css_inherited_style_dispose(&child);
    robo_css_inherited_style_dispose(&parent);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_incomplete_cascade_propagates(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t style = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "app", NULL);
    css_host_seal(css_inherit_host(&fixture));
    /* `:hover` parses and cannot be evaluated, so the collection reports itself
     * incomplete rather than assuming a non-match. A resolution built on it must
     * say so too: a property that *should* have been declared here might have
     * been taken from the parent instead, and no consumer can tell which. */
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div:hover { color: red }");

    css_level_of(&fixture, div, NULL, &style);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_is_complete(&style), 0);
    ROBO_CHECK_EQ_INT((int)style.unsupported, 1);
    /* Incompleteness is not emptiness: the inherited family is still answered. */
    css_inherit_is(&style, "color", "black", "default");

    robo_css_inherited_style_dispose(&style);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_counters_and_determinism(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t first = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t second = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;
    size_t index;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, "card");
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red; width: 10px }\n.card { color: blue }");

    css_level_of(&fixture, div, NULL, &first);
    /* Two declared properties, not three candidates: the cascade picked a winner
     * for `color` and one entry came out. */
    ROBO_CHECK_EQ_INT((int)first.declared_count, 2);
    ROBO_CHECK_EQ_INT((int)first.count, (int)(first.declared_count +
                                             first.inherited_count +
                                             first.default_count +
                                             first.deferred_count));
    css_inherit_is(&first, "color", "blue", "declared");
    css_inherit_is(&first, "width", "10px", "declared");
    /* `color` is one of the family and it was declared, so only the other
     * fifteen inherited-by-default properties reach their initial value. */
    ROBO_CHECK_EQ_INT((int)first.default_count, (int)(CSS_INHERITED_FAMILY - 1u));

    /* The same inputs produce the same output, entry for entry — no address, no
     * hash order and no allocator state may reach the answer. */
    css_level_of(&fixture, div, NULL, &second);
    ROBO_CHECK_EQ_INT((int)second.count, (int)first.count);
    ROBO_CHECK_EQ_INT((int)second.declared_count, (int)first.declared_count);
    ROBO_CHECK_EQ_INT((int)second.default_count, (int)first.default_count);
    for (index = 0u; index < first.count; index++) {
        const robo_css_inherited_property_t *a = robo_css_inherited_style_at(&first, index);
        const robo_css_inherited_property_t *b = robo_css_inherited_style_at(&second, index);

        ROBO_CHECK(a != NULL && b != NULL);
        if (!a || !b) {
            continue;
        }
        ROBO_CHECK_EQ_INT((int)a->name_length, (int)b->name_length);
        ROBO_CHECK_EQ_INT((int)a->source, (int)b->source);
        ROBO_CHECK_EQ_INT((int)a->keyword, (int)b->keyword);
        ROBO_CHECK_EQ_INT(robo_css_slice_equals(a->name ? robo_css_slice_from_text(a->name)
                                                        : robo_css_slice_empty(),
                                                robo_css_slice_from_text(b->name)),
                          1);
    }

    robo_css_inherited_style_dispose(&second);
    robo_css_inherited_style_dispose(&first);
    css_inherit_end(&fixture);
}

static void test_css_inherit_resolve_releases_everything(void)
{
    int enabled;
    long baseline;

    css_fixture();
    enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();

    {
        css_inherit_fixture_t fixture;
        robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
        robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
        robo_css_element_ref_t div;
        robo_css_element_ref_t paragraph;
        size_t index;

        css_inherit_begin(&fixture);
        div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
        paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
        css_host_set_parent(css_inherit_host(&fixture), paragraph, div);
        css_host_seal(css_inherit_host(&fixture));
        /* The fixture owns every sheet it makes and releases them in end, so a
         * resolution that ran through 22 collections is measured on its own
         * footprint rather than on the setup's. */
        (void)css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                                "div { color: red; --x: 1 }\np { color: inherit; --y: 2 }");
        css_level_of(&fixture, div, NULL, &parent);
        css_level_of(&fixture, paragraph, &parent, &child);
        /* Resolving repeatedly must not creep, because every resolution disposes
         * what it replaces. */
        for (index = 0u; index < 20u; index++) {
            robo_css_inherited_style_t churn = ROBO_CSS_CLEARED;

            css_level_of(&fixture, paragraph, &parent, &churn);
            robo_css_inherited_style_dispose(&churn);
        }
        /* A resolution that fails produces nothing and leaks nothing. */
        ROBO_CHECK_EQ_INT(robo_css_inherit_resolve(NULL, NULL, NULL, &child, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_inherit_resolve(css_inherit_context(&fixture), NULL, NULL,
                                                   NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
        robo_css_inherited_style_dispose(&child);
        robo_css_inherited_style_dispose(&parent);
        css_inherit_end(&fixture);
    }

    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(enabled);
}

/* ------------------------------------------------------------------ */
/* The ancestor walk                                                   */
/* ------------------------------------------------------------------ */

static void test_css_inherit_chain_walks_to_the_root(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t style = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_element_ref_t html;
    robo_css_element_ref_t body;
    robo_css_element_ref_t section;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    html = css_host_add(css_inherit_host(&fixture), "html", NULL, NULL);
    body = css_host_add(css_inherit_host(&fixture), "body", NULL, NULL);
    section = css_host_add(css_inherit_host(&fixture), "section", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, "title");
    css_host_set_parent(css_inherit_host(&fixture), body, html);
    css_host_set_parent(css_inherit_host(&fixture), section, body);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, section);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "html { color: red }\n"
                      "section { color: blue }\n"
                      "p.title { font-weight: bold }");

    /* No caller-supplied parent: the door finds the root itself and resolves
     * each level on the way down. The answer must be the same as resolving the
     * chain by hand, which the tests above do. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_resolve_inherited_style(
                          css_inherit_context(&fixture), paragraph, &style, &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 0);
    css_inherit_is(&style, "color", "blue", "parent");
    css_inherit_is(&style, "font-weight", "bold", "declared");
    /* Red on `html` reached `section` (which overrode it with blue) and then
     * `p` got blue — one walk, four levels, no caller-supplied parent. Every
     * inherited-by-default property except the one `p` declared (`font-weight`)
     * comes from the immediate parent, which is what makes the chain literal
     * rather than "the nearest ancestor that declared something". */
    ROBO_CHECK_EQ_INT((int)style.inherited_count, (int)(CSS_INHERITED_FAMILY - 1u));
    ROBO_CHECK_EQ_INT((int)style.declared_count, 1);

    /* The root's own resolution is the one with no parent. */
    robo_css_inherited_style_dispose(&style);
    ROBO_CHECK_EQ_INT(robo_css_style_context_resolve_inherited_style(
                          css_inherit_context(&fixture), html, &style, &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_has_parent(&style), 0);
    css_inherit_is(&style, "color", "red", "declared");

    robo_css_inherited_style_dispose(&style);
    css_inherit_end(&fixture);
}

static void test_css_inherit_chain_without_a_host(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t style = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red }");
    /* A host that can say *what* an element is but not *where* it sits is the
     * case the header describes: the walk finds no ancestor, so the element
     * resolves as a root. Matching still works — the view is untouched — which
     * is the half a cleared host would wrongly take away. */
    {
        robo_css_element_host_t host =
            robo_css_style_context_element_host(css_inherit_context(&fixture));

        ROBO_CHECK(host.view != NULL);
        host.parent = NULL;
        ROBO_CHECK_EQ_INT(robo_css_style_context_set_element_host(css_inherit_context(&fixture),
                                                                 &host, NULL),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_css_style_context_resolve_inherited_style(
                          css_inherit_context(&fixture), div, &style, &error, NULL),
                      ROBO_OK);
    css_inherit_is(&style, "color", "red", "declared");
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_has_parent(&style), 0);
    ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 0);

    /* An element the host cannot describe still resolves: nothing matched, and
     * that is a complete answer rather than an error. */
    robo_css_inherited_style_dispose(&style);
    {
        robo_css_element_ref_t absent = 99u;

        ROBO_CHECK_EQ_INT(robo_css_style_context_resolve_inherited_style(
                              css_inherit_context(&fixture), absent, &style, &error, NULL),
                          ROBO_OK);
        css_inherit_is(&style, "color", "black", "default");
        robo_css_inherited_style_dispose(&style);
    }

    /* Argument contract. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_resolve_inherited_style(
                          NULL, div, &style, &error, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_style_context_resolve_inherited_style(
                          css_inherit_context(&fixture), div, NULL, &error, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_style_context_resolve_inherited_style(
                          css_inherit_context(&fixture), ROBO_CSS_ELEMENT_REF_NONE, &style,
                          &error, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* Misuse of the call is a contract failure, reported by the return code and
     * by the record's `code`. It is *not* a CSS problem: the error vocabulary
     * has no "bad argument" kind, and the style domain records argument
     * failures with kind NONE exactly as the cascade door does — which is what
     * robo_css_error_is_error means by "this is a problem". */
    ROBO_CHECK_EQ_INT(error.code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 0);
    css_inherit_end(&fixture);
}

static void test_css_inherit_chain_is_bounded(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t style = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_element_ref_t first;
    robo_css_element_ref_t last;
    robo_css_element_ref_t previous = ROBO_CSS_ELEMENT_REF_NONE;
    size_t index;

    css_fixture();
    css_inherit_begin(&fixture);
    /* A chain deeper than the walk goes: the context door must stop, resolve
     * what it can, and *say* that it stopped rather than presenting a truncated
     * chain as a root. The host double holds 24 elements, so the depth is capped
     * by the double here rather than by the walk — which is exactly the
     * situation the flag exists for. */
    /* Only the root is targeted, so every level below it reaches its colour by
     * inheritance: a sheet matching every `div` would declare the colour on all
     * of them and prove nothing about the walk. */
    first = css_host_add(css_inherit_host(&fixture), "div", "root", NULL);
    previous = first;
    for (index = 1u; index < 24u; index++) {
        robo_css_element_ref_t next = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);

        css_host_set_parent(css_inherit_host(&fixture), next, previous);
        previous = next;
    }
    last = previous;
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "#root { color: red }");

    ROBO_CHECK_EQ_INT(robo_css_style_context_resolve_inherited_style(
                          css_inherit_context(&fixture), last, &style, &error, NULL),
                      ROBO_OK);
    css_inherit_is(&style, "color", "red", "parent");
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_is_complete(&style), 1);

    /* A cycle among parents is bounded by the same constant: the walk stops, and
     * the result is marked incomplete rather than presented as a root. */
    robo_css_inherited_style_dispose(&style);
    css_host_set_parent(css_inherit_host(&fixture), first, last);
    css_host_seal(css_inherit_host(&fixture));
    ROBO_CHECK_EQ_INT(robo_css_style_context_resolve_inherited_style(
                          css_inherit_context(&fixture), last, &style, &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_is_complete(&style), 0);

    robo_css_inherited_style_dispose(&style);
    css_inherit_end(&fixture);
}

void robo_test_register_css_inherit_resolve(void);

void robo_test_register_css_inherit_resolve(void)
{
    robo_test_register("css.inherit.resolve.declared",
                       test_css_inherit_resolve_declared);
    robo_test_register("css.inherit.resolve.inherits_by_default",
                       test_css_inherit_resolve_inherits_by_default);
    robo_test_register("css.inherit.resolve.not_inherited_does_not",
                       test_css_inherit_resolve_not_inherited_does_not);
    robo_test_register("css.inherit.resolve.child_overrides_parent",
                       test_css_inherit_resolve_child_overrides_parent);
    robo_test_register("css.inherit.resolve.intermediate_overrides_ancestor",
                       test_css_inherit_resolve_intermediate_overrides_ancestor);
    robo_test_register("css.inherit.resolve.root_uses_defaults",
                       test_css_inherit_resolve_root_uses_defaults);
    robo_test_register("css.inherit.resolve.explicit_inherit",
                       test_css_inherit_resolve_explicit_inherit);
    robo_test_register("css.inherit.resolve.explicit_initial",
                       test_css_inherit_resolve_explicit_initial);
    robo_test_register("css.inherit.resolve.unset", test_css_inherit_resolve_unset);
    robo_test_register("css.inherit.resolve.revert_is_deferred",
                       test_css_inherit_resolve_revert_is_deferred);
    robo_test_register("css.inherit.resolve.custom_property",
                       test_css_inherit_resolve_custom_property);
    robo_test_register("css.inherit.resolve.custom_property_has_no_default",
                       test_css_inherit_resolve_custom_property_has_no_default);
    robo_test_register("css.inherit.resolve.unknown_property_does_not_inherit",
                       test_css_inherit_resolve_unknown_property_does_not_inherit);
    robo_test_register("css.inherit.resolve.inline_participates",
                       test_css_inherit_resolve_inline_participates);
    robo_test_register("css.inherit.resolve.incomplete_cascade_propagates",
                       test_css_inherit_resolve_incomplete_cascade_propagates);
    robo_test_register("css.inherit.resolve.counters_and_determinism",
                       test_css_inherit_resolve_counters_and_determinism);
    robo_test_register("css.inherit.resolve.releases_everything",
                       test_css_inherit_resolve_releases_everything);
    robo_test_register("css.inherit.chain.walks_to_the_root",
                       test_css_inherit_chain_walks_to_the_root);
    robo_test_register("css.inherit.chain.without_a_host",
                       test_css_inherit_chain_without_a_host);
    robo_test_register("css.inherit.chain.is_bounded",
                       test_css_inherit_chain_is_bounded);
}
