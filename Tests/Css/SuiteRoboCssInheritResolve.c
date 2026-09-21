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
static void CssLevelOf(CssInheritFixture *fixture,
                         CssElementRef ref,
                         const CssInheritedStyle *parent,
                         CssInheritedStyle *out)
{
    CssCascade cascade = RoboCssCleared;

    RoboCssCascadeCollect(&fixture->cascade, ref, &cascade, NULL);
    CssInheritFixtureResolve(fixture, &cascade, parent, out);
    CssCascadeDispose(&cascade);
}

/* ------------------------------------------------------------------ */
/* What the element declares                                           */
/* ------------------------------------------------------------------ */

static void TestCssInheritResolveDeclared(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle style = RoboCssCleared;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "div { color: red }");

    CssLevelOf(&fixture, div, NULL, &style);
    CssInheritIs(&style, "color", "red", "declared");
    RoboCheckStrEq(CssInheritKeyword(&style, "color"), "none");
    RoboCheckEqInt(style.declaredCount, 1u);
    RoboCheckEqInt((int)CssInheritedStyleElement(&style), (int)div);
    RoboCheckEqInt(CssInheritedStyleHasParent(&style), 0);
    /* The entry carries the behaviour's identity as well as its name. */
    RoboCheck(CssInheritedStyleFindProperty(
                   &style, CssPropertyTableLookup(fixture.table, "color")) != NULL);
    RoboCheck(CssInheritedPropertyValue(
                   CssInheritedStyleFind(&style, CssSliceFromText("color"))) !=
               NULL);

    CssInheritedStyleDispose(&style);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveInheritsByDefault(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle parent = RoboCssCleared;
    CssInheritedStyle child = RoboCssCleared;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "div { color: red }");

    CssLevelOf(&fixture, div, NULL, &parent);
    CssLevelOf(&fixture, paragraph, &parent, &child);
    /* The paragraph says nothing, and `color` inherits: the parent's value, and
     * the provenance says *why* — default inheritance, no keyword involved. */
    CssInheritIs(&child, "color", "red", "parent");
    RoboCheckStrEq(CssInheritKeyword(&child, "color"), "none");
    RoboCheckEqInt(CssInheritedStyleHasParent(&child), 1);
    RoboCheckEqInt(child.declaredCount, 0u);
    /* The whole inherited family crossed, which is what makes the one assertion
     * above a statement about `color` rather than about the only entry. */
    RoboCheckEqInt(child.inheritedCount, CSS_INHERITED_FAMILY);
    /* The child's own copy: the parent can go away without touching it. */
    CssInheritedStyleDispose(&parent);
    CssInheritIs(&child, "color", "red", "parent");

    CssInheritedStyleDispose(&child);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveNotInheritedDoesNot(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle parent = RoboCssCleared;
    CssInheritedStyle child = RoboCssCleared;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    /* The parent declares the whole non-inherited family. */
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { width: 500px; height: 100px; margin: 10px; padding: 4px; "
                      "background: white; border: 1px solid red; display: block; "
                      "position: absolute }");

    CssLevelOf(&fixture, div, NULL, &parent);
    /* Every one of them is on the parent. */
    CssInheritIs(&parent, "width", "500px", "declared");
    CssInheritIs(&parent, "display", "block", "declared");
    RoboCheck(CssInheritedStyleHas(&parent, CssSliceFromText("margin")));

    CssLevelOf(&fixture, paragraph, &parent, &child);
    /* And none of them is on the child: a non-inherited property nothing
     * declares has no entry at all. Filling one in with `auto` here would be a
     * value no input produced. */
    CssInheritIs(&child, "width", "<absent>", "<absent>");
    CssInheritIs(&child, "height", "<absent>", "<absent>");
    CssInheritIs(&child, "margin", "<absent>", "<absent>");
    CssInheritIs(&child, "padding", "<absent>", "<absent>");
    CssInheritIs(&child, "background", "<absent>", "<absent>");
    CssInheritIs(&child, "border", "<absent>", "<absent>");
    CssInheritIs(&child, "display", "<absent>", "<absent>");
    CssInheritIs(&child, "position", "<absent>", "<absent>");
    /* Only the inherited family crossed, which is what makes the negative
     * assertions above a statement about inheritance rather than about an empty
     * result. */
    RoboCheckEqInt(child.inheritedCount, CSS_INHERITED_FAMILY);
    RoboCheckEqInt(child.defaultCount, 0u);
    CssInheritIs(&child, "color", "black", "parent");

    CssInheritedStyleDispose(&child);
    CssInheritedStyleDispose(&parent);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveChildOverridesParent(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle parent = RoboCssCleared;
    CssInheritedStyle child = RoboCssCleared;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red }\np { color: green }");

    CssLevelOf(&fixture, div, NULL, &parent);
    CssLevelOf(&fixture, paragraph, &parent, &child);
    /* The child's own declaration wins, and inheritance never competes with it:
     * the entry is DECLARED, not PARENT. */
    CssInheritIs(&child, "color", "green", "declared");
    RoboCheckEqInt(child.declaredCount, 1u);
    /* The other fifteen inherited properties still came from the parent, so the
     * override is a statement about `color` and not about inheritance stopping. */
    RoboCheckEqInt(child.inheritedCount, CSS_INHERITED_FAMILY - 1u);

    CssInheritedStyleDispose(&child);
    CssInheritedStyleDispose(&parent);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveIntermediateOverridesAncestor(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle divStyle = RoboCssCleared;
    CssInheritedStyle sectionStyle = RoboCssCleared;
    CssInheritedStyle paragraphStyle = RoboCssCleared;
    CssElementRef div;
    CssElementRef section;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    section = CssHostAdd(CssInheritHost(&fixture), "section", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), section, div);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, section);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red }\nsection { color: blue }");

    CssLevelOf(&fixture, div, NULL, &divStyle);
    CssLevelOf(&fixture, section, &divStyle, &sectionStyle);
    CssLevelOf(&fixture, paragraph, &sectionStyle, &paragraphStyle);

    CssInheritIs(&divStyle, "color", "red", "declared");
    CssInheritIs(&sectionStyle, "color", "blue", "declared");
    /* Blue, not red: the child takes the *immediate* parent's resolved value,
     * not the nearest ancestor that happened to declare something. An
     * implementation that walked up looking for a declaration would answer red
     * here and be wrong in a way that looks reasonable. */
    CssInheritIs(&paragraphStyle, "color", "blue", "parent");
    RoboCheckEqInt(paragraphStyle.inheritedCount, CSS_INHERITED_FAMILY);
    RoboCheckEqInt(paragraphStyle.declaredCount, 0u);

    CssInheritedStyleDispose(&paragraphStyle);
    CssInheritedStyleDispose(&sectionStyle);
    CssInheritedStyleDispose(&divStyle);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* No parent                                                           */
/* ------------------------------------------------------------------ */

static void TestCssInheritResolveRootUsesDefaults(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle style = RoboCssCleared;
    CssElementRef root;

    CssFixture();
    CssInheritBegin(&fixture);
    root = CssHostAdd(CssInheritHost(&fixture), "html", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));

    CssLevelOf(&fixture, root, NULL, &style);
    RoboCheckEqInt(CssInheritedStyleHasParent(&style), 0);
    /* A root declares nothing and still has an answer for every inherited
     * property, from the registry's initial values. This is what makes a chain
     * terminate: at some point "the parent's value" has to become a real value. */
    CssInheritIs(&style, "color", "black", "default");
    CssInheritIs(&style, "font-family", "serif", "default");
    CssInheritIs(&style, "font-size", "medium", "default");
    CssInheritIs(&style, "line-height", "normal", "default");
    CssInheritIs(&style, "visibility", "visible", "default");
    CssInheritIs(&style, "white-space", "normal", "default");
    CssInheritIs(&style, "text-align", "start", "default");
    CssInheritIs(&style, "font-weight", "normal", "default");
    CssInheritIs(&style, "font-style", "normal", "default");
    /* And *only* the inherited family: a root has no `width` entry. */
    CssInheritIs(&style, "width", "<absent>", "<absent>");
    CssInheritIs(&style, "display", "<absent>", "<absent>");
    RoboCheckEqInt((int)style.defaultCount, (int)CSS_INHERITED_FAMILY);
    RoboCheckEqInt((int)style.declaredCount, 0);
    RoboCheckEqInt((int)style.inheritedCount, 0);
    /* Nothing here was refused, so the answer is complete. */
    RoboCheckEqInt(CssInheritedStyleIsComplete(&style), 1);
    RoboCheckEqInt((int)style.unsupported, 0);

    /* A root that *does* declare an inherited property overrides its default. */
    CssInheritedStyleDispose(&style);
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "html { color: navy }");
    CssLevelOf(&fixture, root, NULL, &style);
    CssInheritIs(&style, "color", "navy", "declared");
    CssInheritIs(&style, "font-family", "serif", "default");

    CssInheritedStyleDispose(&style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The CSS-wide keywords                                               */
/* ------------------------------------------------------------------ */

static void TestCssInheritResolveExplicitInherit(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle parent = RoboCssCleared;
    CssInheritedStyle child = RoboCssCleared;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    /* `width` does not inherit, and `inherit` makes it anyway — which is the
     * whole reason the keyword exists. */
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { width: 500px }\np { width: inherit }");

    CssLevelOf(&fixture, div, NULL, &parent);
    CssLevelOf(&fixture, paragraph, &parent, &child);
    CssInheritIs(&child, "width", "500px", "parent");
    RoboCheckStrEq(CssInheritKeyword(&child, "width"), "inherit");
    /* The keyword plus the whole inherited family, all from the parent. */
    RoboCheckEqInt(child.inheritedCount, CSS_INHERITED_FAMILY + 1u);
    RoboCheckEqInt(child.declaredCount, 0u);
    /* The value is the parent's *representation*: `500px`, not a used width. */
    RoboCheckEqInt(CssValueIsEmpty(
                          CssInheritedPropertyValue(
                              CssInheritedStyleFind(&child,
                                                            CssSliceFromText("width")))),
                      0);

    /* `inherit` with no parent value is the parent's computed value, and for an
     * undeclared property that is its initial value. The keyword is still
     * recorded, so the two routes stay distinguishable. */
    CssInheritedStyleDispose(&child);
    CssInheritedStyleDispose(&parent);
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red }\ndiv { width: inherit }");
    CssLevelOf(&fixture, div, NULL, &child);
    CssInheritIs(&child, "width", "auto", "default");
    RoboCheckStrEq(CssInheritKeyword(&child, "width"), "inherit");

    CssInheritedStyleDispose(&child);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveExplicitInitial(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle parent = RoboCssCleared;
    CssInheritedStyle child = RoboCssCleared;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red }\np { color: initial }");

    CssLevelOf(&fixture, div, NULL, &parent);
    CssLevelOf(&fixture, paragraph, &parent, &child);
    /* Not red: `initial` refuses the parent's value, and the result is the
     * property's own initial value rather than a miss. */
    CssInheritIs(&child, "color", "black", "default");
    RoboCheckStrEq(CssInheritKeyword(&child, "color"), "initial");
    RoboCheckEqInt(child.defaultCount, 1u);
    /* `color` was refused; the other fifteen were not. */
    RoboCheckEqInt(child.inheritedCount, CSS_INHERITED_FAMILY - 1u);

    CssInheritedStyleDispose(&child);
    CssInheritedStyleDispose(&parent);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveUnset(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle parent = RoboCssCleared;
    CssInheritedStyle child = RoboCssCleared;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red; width: 500px }\n"
                      "p { color: unset; width: unset }");

    CssLevelOf(&fixture, div, NULL, &parent);
    CssLevelOf(&fixture, paragraph, &parent, &child);
    /* One keyword, two behaviours: `color` inherits like `inherit`, `width`
     * does not and takes its initial value like `initial`. The keyword is
     * recorded on both, which is what keeps "unset" readable as itself. */
    CssInheritIs(&child, "color", "red", "parent");
    RoboCheckStrEq(CssInheritKeyword(&child, "color"), "unset");
    CssInheritIs(&child, "width", "auto", "default");
    RoboCheckStrEq(CssInheritKeyword(&child, "width"), "unset");
    /* The whole inherited family comes from the parent: `color` because `unset`
     * made it inherit, the other fifteen because `color` and `font-*` and the
     * rest inherit by default. `width` is the only default. No `+1`: unlike
     * `width: inherit`, the keyword here lands on a property that was already
     * part of the family, so it adds no entry. */
    RoboCheckEqInt(child.inheritedCount, CSS_INHERITED_FAMILY);
    RoboCheckEqInt(child.defaultCount, 1u);
    RoboCheckEqInt(child.declaredCount, 0u);

    CssInheritedStyleDispose(&child);
    CssInheritedStyleDispose(&parent);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveRevertIsDeferred(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle style = RoboCssCleared;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "div { color: revert }");

    CssLevelOf(&fixture, div, NULL, &style);
    /* Recognised, carried, and *reported*: the value is the keyword itself and
     * the entry says it is deferred, so nothing can read it as an answer. An
     * implementation that quietly treated `revert` as `unset` would show up
     * here as `black` with source default. */
    CssInheritIs(&style, "color", "revert", "deferred");
    RoboCheckStrEq(CssInheritKeyword(&style, "color"), "revert");
    RoboCheckEqInt(style.deferredCount, 1u);
    RoboCheckEqInt(style.declaredCount, 0u);
    /* The other fifteen inherited properties are still answered normally: a
     * deferred keyword does not poison the properties beside it. */
    RoboCheckEqInt(style.defaultCount, CSS_INHERITED_FAMILY - 1u);
    RoboCheckEqInt(CssInheritedStyleIsComplete(&style), 0);
    RoboCheckEqInt((int)style.unsupported, 1);

    CssInheritedStyleDispose(&style);

    /* A `revert` the parent carried travels down as one — the author's
     * declaration is never silently dropped on the way. */
    {
        CssInheritedStyle child = RoboCssCleared;
        CssElementRef paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);

        CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
        CssHostSeal(CssInheritHost(&fixture));
        CssLevelOf(&fixture, div, NULL, &style);
        CssLevelOf(&fixture, paragraph, &style, &child);
        CssInheritIs(&child, "color", "revert", "deferred");
        RoboCheckEqInt(child.deferredCount, 1u);
        CssInheritedStyleDispose(&child);
        CssInheritedStyleDispose(&style);
    }
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Custom properties                                                   */
/* ------------------------------------------------------------------ */

static void TestCssInheritResolveCustomProperty(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle parent = RoboCssCleared;
    CssInheritedStyle child = RoboCssCleared;
    CssElementRef root;
    CssElementRef childRef;

    CssFixture();
    CssInheritBegin(&fixture);
    root = CssHostAdd(CssInheritHost(&fixture), "html", NULL, NULL);
    childRef = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), childRef, root);
    CssHostSeal(CssInheritHost(&fixture));
    /* A custom property has no identity in a table, so its *name* is what
     * identifies it — and CSS defines it as inherited. */
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      ":root { --theme-color: blue }\np { --own: 1px }");

    CssLevelOf(&fixture, root, NULL, &parent);
    RoboCheckStrEq(CssInheritSource(&parent, "--theme-color"), "declared");
    /* A custom property has no identity in a table, so its name is its identity
     * — and the entry says so. */
    {
        const CssInheritedProperty *custom = CssInheritedStyleFind(
            &parent, CssSliceFromText("--theme-color"));

        RoboCheck(custom != NULL);
        if (custom) {
            RoboCheckEqInt((int)custom->property, (int)RoboCssPropertyIdNone);
            RoboCheckEqInt((int)custom->nameLength, (int)strlen("--theme-color"));
        }
    }

    CssLevelOf(&fixture, childRef, &parent, &child);
    /* Carried down the chain, value and all: the raw tokens are preserved so
     * 9.8's `var()` substitution has something to read. */
    CssInheritIs(&child, "--theme-color", "blue", "parent");
    CssInheritIs(&child, "--own", "1px", "declared");

    /* And a child can override one, without disturbing the other. */
    CssInheritedStyleDispose(&child);
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "p { --theme-color: green }");
    CssLevelOf(&fixture, childRef, &parent, &child);
    CssInheritIs(&child, "--theme-color", "green", "declared");
    CssInheritIs(&child, "--own", "1px", "declared");

    CssInheritedStyleDispose(&child);
    CssInheritedStyleDispose(&parent);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveCustomPropertyHasNoDefault(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle style = RoboCssCleared;
    CssElementRef root;

    CssFixture();
    CssInheritBegin(&fixture);
    root = CssHostAdd(CssInheritHost(&fixture), "html", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));

    CssLevelOf(&fixture, root, NULL, &style);
    /* An unset custom property is *invalid*, not `initial`: CSS gives it no
     * initial value, so there is no entry and nothing is defaulted. A registry
     * that happened to know a `--x` must not change that. */
    CssInheritIs(&style, "--theme-color", "<absent>", "<absent>");
    RoboCheckEqInt((int)style.defaultCount, (int)CSS_INHERITED_FAMILY);
    RoboCheckEqInt((int)style.unresolvedCount, 0);

    /* `initial` on a custom property is the same answer: CSS drops it rather
     * than giving it a value. */
    CssInheritedStyleDispose(&style);
    CssInheritSheet(&fixture, RoboCssOriginAuthor, ":root { --x: initial }");
    CssLevelOf(&fixture, root, NULL, &style);
    CssInheritIs(&style, "--x", "<unresolved>", "default");
    RoboCheckEqInt((int)style.unresolvedCount, 1);
    RoboCheckEqInt((int)style.unsupported, 1);

    CssInheritedStyleDispose(&style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Unknown properties                                                  */
/* ------------------------------------------------------------------ */

static void TestCssInheritResolveUnknownPropertyDoesNotInherit(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle parent = RoboCssCleared;
    CssInheritedStyle child = RoboCssCleared;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { totally-unknown-property: whatever }");

    CssLevelOf(&fixture, div, NULL, &parent);
    /* The value is preserved — the author wrote it, and 9.5 already decided not
     * to lose it — but a property nothing can interpret must not spread down a
     * tree. */
    CssInheritIs(&parent, "totally-unknown-property", "whatever", "declared");
    CssLevelOf(&fixture, paragraph, &parent, &child);
    CssInheritIs(&child, "totally-unknown-property", "<absent>", "<absent>");

    /* `initial` on an unknown property is typed rather than invented: the entry
     * exists, says its default is not known, and is counted. */
    CssInheritedStyleDispose(&child);
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "p { totally-unknown-property: initial }");
    CssLevelOf(&fixture, paragraph, &parent, &child);
    CssInheritIs(&child, "totally-unknown-property", "<unresolved>", "default");
    RoboCheckEqInt((int)child.unresolvedCount, 1);
    RoboCheckEqInt(CssInheritedStyleIsComplete(&child), 0);

    CssInheritedStyleDispose(&child);
    CssInheritedStyleDispose(&parent);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Cascade interaction                                                 */
/* ------------------------------------------------------------------ */

static void TestCssInheritResolveInlineParticipates(void)
{
    CssInheritFixture fixture;
    CssInlineStub stub;
    CssInheritedStyle parent = RoboCssCleared;
    CssInheritedStyle child = RoboCssCleared;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "p { color: red }");

    /* One `style` attribute, handed out for whichever element is asked — the
     * stub models a document with one sloppily, which is all these two cases
     * need. */
    CssInheritInline(&fixture, &stub, "color: purple");
    CssLevelOf(&fixture, div, NULL, &parent);
    CssInheritIs(&parent, "color", "purple", "declared");

    /* A `style` attribute on the parent reaches the child through inheritance,
     * and the child's own selector declaration still beats it. */
    CssInheritedStyleDispose(&parent);
    CssInheritInline(&fixture, &stub, "color: purple");
    CssLevelOf(&fixture, paragraph, NULL, &child);
    CssInheritIs(&child, "color", "purple", "declared");
    RoboCheckEqInt(child.declaredCount, 1u);

    /* An `!important` on the *parent* has no way to reach the child, because
     * importance is a fact about a declaration on this element and inheritance
     * is not a declaration. */
    CssInheritEnd(&fixture);

    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red !important }\np { color: blue }");
    CssLevelOf(&fixture, div, NULL, &parent);
    CssInheritIs(&parent, "color", "red", "declared");
    CssLevelOf(&fixture, paragraph, &parent, &child);
    CssInheritIs(&child, "color", "blue", "declared");

    CssInheritedStyleDispose(&child);
    CssInheritedStyleDispose(&parent);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveIncompleteCascadePropagates(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle style = RoboCssCleared;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "app", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    /* `:hover` parses and cannot be evaluated, so the collection reports itself
     * incomplete rather than assuming a non-match. A resolution built on it must
     * say so too: a property that *should* have been declared here might have
     * been taken from the parent instead, and no consumer can tell which. */
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "div:hover { color: red }");

    CssLevelOf(&fixture, div, NULL, &style);
    RoboCheckEqInt(CssInheritedStyleIsComplete(&style), 0);
    RoboCheckEqInt((int)style.unsupported, 1);
    /* Incompleteness is not emptiness: the inherited family is still answered. */
    CssInheritIs(&style, "color", "black", "default");

    CssInheritedStyleDispose(&style);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveCountersAndDeterminism(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle first = RoboCssCleared;
    CssInheritedStyle second = RoboCssCleared;
    CssElementRef div;
    size_t index;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, "card");
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red; width: 10px }\n.card { color: blue }");

    CssLevelOf(&fixture, div, NULL, &first);
    /* Two declared properties, not three candidates: the cascade picked a winner
     * for `color` and one entry came out. */
    RoboCheckEqInt((int)first.declaredCount, 2);
    RoboCheckEqInt((int)first.count, (int)(first.declaredCount +
                                             first.inheritedCount +
                                             first.defaultCount +
                                             first.deferredCount));
    CssInheritIs(&first, "color", "blue", "declared");
    CssInheritIs(&first, "width", "10px", "declared");
    /* `color` is one of the family and it was declared, so only the other
     * fifteen inherited-by-default properties reach their initial value. */
    RoboCheckEqInt((int)first.defaultCount, (int)(CSS_INHERITED_FAMILY - 1u));

    /* The same inputs produce the same output, entry for entry — no address, no
     * hash order and no allocator state may reach the answer. */
    CssLevelOf(&fixture, div, NULL, &second);
    RoboCheckEqInt((int)second.count, (int)first.count);
    RoboCheckEqInt((int)second.declaredCount, (int)first.declaredCount);
    RoboCheckEqInt((int)second.defaultCount, (int)first.defaultCount);
    for (index = 0u; index < first.count; index++) {
        const CssInheritedProperty *a = CssInheritedStyleAt(&first, index);
        const CssInheritedProperty *b = CssInheritedStyleAt(&second, index);

        RoboCheck(a != NULL && b != NULL);
        if (!a || !b) {
            continue;
        }
        RoboCheckEqInt((int)a->nameLength, (int)b->nameLength);
        RoboCheckEqInt((int)a->source, (int)b->source);
        RoboCheckEqInt((int)a->keyword, (int)b->keyword);
        RoboCheckEqInt(CssSliceEquals(a->name ? CssSliceFromText(a->name)
                                                        : CssSliceEmpty(),
                                                CssSliceFromText(b->name)),
                          1);
    }

    CssInheritedStyleDispose(&second);
    CssInheritedStyleDispose(&first);
    CssInheritEnd(&fixture);
}

static void TestCssInheritResolveReleasesEverything(void)
{
    int enabled;
    long baseline;

    CssFixture();
    enabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();

    {
        CssInheritFixture fixture;
        CssInheritedStyle parent = RoboCssCleared;
        CssInheritedStyle child = RoboCssCleared;
        CssElementRef div;
        CssElementRef paragraph;
        size_t index;

        CssInheritBegin(&fixture);
        div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
        paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
        CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
        CssHostSeal(CssInheritHost(&fixture));
        /* The fixture owns every sheet it makes and releases them in end, so a
         * resolution that ran through 22 collections is measured on its own
         * footprint rather than on the setup's. */
        (void)CssInheritSheet(&fixture, RoboCssOriginAuthor,
                                "div { color: red; --x: 1 }\np { color: inherit; --y: 2 }");
        CssLevelOf(&fixture, div, NULL, &parent);
        CssLevelOf(&fixture, paragraph, &parent, &child);
        /* Resolving repeatedly must not creep, because every resolution disposes
         * what it replaces. */
        for (index = 0u; index < 20u; index++) {
            CssInheritedStyle churn = RoboCssCleared;

            CssLevelOf(&fixture, paragraph, &parent, &churn);
            CssInheritedStyleDispose(&churn);
        }
        /* A resolution that fails produces nothing and leaks nothing. */
        RoboCheckEqInt(RoboCssInheritResolve(NULL, NULL, NULL, &child, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(RoboCssInheritResolve(CssInheritContext(&fixture), NULL, NULL,
                                                   NULL, NULL),
                          RoboInvalidArgument);
        CssInheritedStyleDispose(&child);
        CssInheritedStyleDispose(&parent);
        CssInheritEnd(&fixture);
    }

    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(enabled);
}

/* ------------------------------------------------------------------ */
/* The ancestor walk                                                   */
/* ------------------------------------------------------------------ */

static void TestCssInheritChainWalksToTheRoot(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle style = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssElementRef html;
    CssElementRef body;
    CssElementRef section;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    html = CssHostAdd(CssInheritHost(&fixture), "html", NULL, NULL);
    body = CssHostAdd(CssInheritHost(&fixture), "body", NULL, NULL);
    section = CssHostAdd(CssInheritHost(&fixture), "section", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, "title");
    CssHostSetParent(CssInheritHost(&fixture), body, html);
    CssHostSetParent(CssInheritHost(&fixture), section, body);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, section);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "html { color: red }\n"
                      "section { color: blue }\n"
                      "p.title { font-weight: bold }");

    /* No caller-supplied parent: the door finds the root itself and resolves
     * each level on the way down. The answer must be the same as resolving the
     * chain by hand, which the tests above do. */
    RoboCheckEqInt(CssStyleContextResolveInheritedStyle(
                          CssInheritContext(&fixture), paragraph, &style, &error, NULL),
                      RoboOk);
    RoboCheckEqInt(CssErrorIsError(&error), 0);
    CssInheritIs(&style, "color", "blue", "parent");
    CssInheritIs(&style, "font-weight", "bold", "declared");
    /* Red on `html` reached `section` (which overrode it with blue) and then
     * `p` got blue — one walk, four levels, no caller-supplied parent. Every
     * inherited-by-default property except the one `p` declared (`font-weight`)
     * comes from the immediate parent, which is what makes the chain literal
     * rather than "the nearest ancestor that declared something". */
    RoboCheckEqInt((int)style.inheritedCount, (int)(CSS_INHERITED_FAMILY - 1u));
    RoboCheckEqInt((int)style.declaredCount, 1);

    /* The root's own resolution is the one with no parent. */
    CssInheritedStyleDispose(&style);
    RoboCheckEqInt(CssStyleContextResolveInheritedStyle(
                          CssInheritContext(&fixture), html, &style, &error, NULL),
                      RoboOk);
    RoboCheckEqInt(CssInheritedStyleHasParent(&style), 0);
    CssInheritIs(&style, "color", "red", "declared");

    CssInheritedStyleDispose(&style);
    CssInheritEnd(&fixture);
}

static void TestCssInheritChainWithoutAHost(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle style = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "div { color: red }");
    /* A host that can say *what* an element is but not *where* it sits is the
     * case the header describes: the walk finds no ancestor, so the element
     * resolves as a root. Matching still works — the view is untouched — which
     * is the half a cleared host would wrongly take away. */
    {
        CssElementHost host =
            CssStyleContextElementHost(CssInheritContext(&fixture));

        RoboCheck(host.view != NULL);
        host.parent = NULL;
        RoboCheckEqInt(CssStyleContextSetElementHost(CssInheritContext(&fixture),
                                                                 &host, NULL),
                          RoboOk);
    }
    RoboCheckEqInt(CssStyleContextResolveInheritedStyle(
                          CssInheritContext(&fixture), div, &style, &error, NULL),
                      RoboOk);
    CssInheritIs(&style, "color", "red", "declared");
    RoboCheckEqInt(CssInheritedStyleHasParent(&style), 0);
    RoboCheckEqInt(CssErrorIsError(&error), 0);

    /* An element the host cannot describe still resolves: nothing matched, and
     * that is a complete answer rather than an error. */
    CssInheritedStyleDispose(&style);
    {
        CssElementRef absent = 99u;

        RoboCheckEqInt(CssStyleContextResolveInheritedStyle(
                              CssInheritContext(&fixture), absent, &style, &error, NULL),
                          RoboOk);
        CssInheritIs(&style, "color", "black", "default");
        CssInheritedStyleDispose(&style);
    }

    /* Argument contract. */
    RoboCheckEqInt(CssStyleContextResolveInheritedStyle(
                          NULL, div, &style, &error, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStyleContextResolveInheritedStyle(
                          CssInheritContext(&fixture), div, NULL, &error, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStyleContextResolveInheritedStyle(
                          CssInheritContext(&fixture), RoboCssElementRefNone, &style,
                          &error, NULL),
                      RoboInvalidArgument);
    /* Misuse of the call is a contract failure, reported by the return code and
     * by the record's `code`. It is *not* a CSS problem: the error vocabulary
     * has no "bad argument" kind, and the style domain records argument
     * failures with kind NONE exactly as the cascade door does — which is what
     * CssErrorIsError means by "this is a problem". */
    RoboCheckEqInt(error.code, RoboInvalidArgument);
    RoboCheckEqInt(CssErrorIsError(&error), 0);
    CssInheritEnd(&fixture);
}

static void TestCssInheritChainIsBounded(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle style = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssElementRef first;
    CssElementRef last;
    CssElementRef previous = RoboCssElementRefNone;
    size_t index;

    CssFixture();
    CssInheritBegin(&fixture);
    /* A chain deeper than the walk goes: the context door must stop, resolve
     * what it can, and *say* that it stopped rather than presenting a truncated
     * chain as a root. The host double holds 24 elements, so the depth is capped
     * by the double here rather than by the walk — which is exactly the
     * situation the flag exists for. */
    /* Only the root is targeted, so every level below it reaches its colour by
     * inheritance: a sheet matching every `div` would declare the colour on all
     * of them and prove nothing about the walk. */
    first = CssHostAdd(CssInheritHost(&fixture), "div", "root", NULL);
    previous = first;
    for (index = 1u; index < 24u; index++) {
        CssElementRef next = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);

        CssHostSetParent(CssInheritHost(&fixture), next, previous);
        previous = next;
    }
    last = previous;
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "#root { color: red }");

    RoboCheckEqInt(CssStyleContextResolveInheritedStyle(
                          CssInheritContext(&fixture), last, &style, &error, NULL),
                      RoboOk);
    CssInheritIs(&style, "color", "red", "parent");
    RoboCheckEqInt(CssInheritedStyleIsComplete(&style), 1);

    /* A cycle among parents is bounded by the same constant: the walk stops, and
     * the result is marked incomplete rather than presented as a root. */
    CssInheritedStyleDispose(&style);
    CssHostSetParent(CssInheritHost(&fixture), first, last);
    CssHostSeal(CssInheritHost(&fixture));
    RoboCheckEqInt(CssStyleContextResolveInheritedStyle(
                          CssInheritContext(&fixture), last, &style, &error, NULL),
                      RoboOk);
    RoboCheckEqInt(CssInheritedStyleIsComplete(&style), 0);

    CssInheritedStyleDispose(&style);
    CssInheritEnd(&fixture);
}

void TestRegisterCssInheritResolve(void);

void TestRegisterCssInheritResolve(void)
{
    TestRegister("css.inherit.resolve.declared",
                       TestCssInheritResolveDeclared);
    TestRegister("css.inherit.resolve.inherits_by_default",
                       TestCssInheritResolveInheritsByDefault);
    TestRegister("css.inherit.resolve.not_inherited_does_not",
                       TestCssInheritResolveNotInheritedDoesNot);
    TestRegister("css.inherit.resolve.child_overrides_parent",
                       TestCssInheritResolveChildOverridesParent);
    TestRegister("css.inherit.resolve.intermediate_overrides_ancestor",
                       TestCssInheritResolveIntermediateOverridesAncestor);
    TestRegister("css.inherit.resolve.root_uses_defaults",
                       TestCssInheritResolveRootUsesDefaults);
    TestRegister("css.inherit.resolve.explicit_inherit",
                       TestCssInheritResolveExplicitInherit);
    TestRegister("css.inherit.resolve.explicit_initial",
                       TestCssInheritResolveExplicitInitial);
    TestRegister("css.inherit.resolve.unset", TestCssInheritResolveUnset);
    TestRegister("css.inherit.resolve.revert_is_deferred",
                       TestCssInheritResolveRevertIsDeferred);
    TestRegister("css.inherit.resolve.custom_property",
                       TestCssInheritResolveCustomProperty);
    TestRegister("css.inherit.resolve.custom_property_has_no_default",
                       TestCssInheritResolveCustomPropertyHasNoDefault);
    TestRegister("css.inherit.resolve.unknown_property_does_not_inherit",
                       TestCssInheritResolveUnknownPropertyDoesNotInherit);
    TestRegister("css.inherit.resolve.inline_participates",
                       TestCssInheritResolveInlineParticipates);
    TestRegister("css.inherit.resolve.incomplete_cascade_propagates",
                       TestCssInheritResolveIncompleteCascadePropagates);
    TestRegister("css.inherit.resolve.counters_and_determinism",
                       TestCssInheritResolveCountersAndDeterminism);
    TestRegister("css.inherit.resolve.releases_everything",
                       TestCssInheritResolveReleasesEverything);
    TestRegister("css.inherit.chain.walks_to_the_root",
                       TestCssInheritChainWalksToTheRoot);
    TestRegister("css.inherit.chain.without_a_host",
                       TestCssInheritChainWithoutAHost);
    TestRegister("css.inherit.chain.is_bounded",
                       TestCssInheritChainIsBounded);
}
