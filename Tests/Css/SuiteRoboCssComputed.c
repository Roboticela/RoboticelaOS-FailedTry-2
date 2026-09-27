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
static void CssComputeOf(CssInheritFixture *fixture,
                           CssElementRef ref,
                           CssComputedStyle *out)
{
    CssError error = RoboCssCleared;

    RoboCheckEqInt(CssStyleContextComputeStyle(fixture->cascade.context, ref,
                                                          out, &error, NULL),
                      RoboOk);
    /* A clean document produces no CSS problem, which is what makes the
     * expectations below about the values rather than about the parse. */
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
}

/** Assert the query door answers @p name with @p expected and @p fromInitial. */
static void CssComputeQuery(CssInheritFixture *fixture,
                              const CssComputedStyle *style,
                              const char *name,
                              const char *expected,
                              int fromInitial)
{
    CssComputedValue value = RoboCssCleared;
    char text[RoboCssComputedValueTextMax];
    size_t length = 0u;
    int initial = -1;

    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          fixture->cascade.context, style, CssSliceFromText(name),
                          &value, &initial, NULL),
                      RoboOk);
    RoboCheckEqInt(initial, fromInitial);
    text[0] = '\0';
    RoboCheckEqInt(CssComputedValueWrite(&value, text, sizeof(text), &length,
                                                    NULL),
                      RoboOk);
    RoboCheckStrEq(text, expected);
    CssComputedValueDispose(&value);
}

/* ------------------------------------------------------------------ */
/* One element, declared values                                       */
/* ------------------------------------------------------------------ */

static void TestCssComputedDeclared(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: #336699; width: 50%; height: 2em; display: block }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssComputeOf(&fixture, div, style);

    CssComputedIs(style, "color", "#336699");
    CssComputedFlagsAre(style, "color", "declared");
    /* A percentage stays a percentage: only layout knows what it is a
     * percentage *of*, and turning it into pixels here would be a made-up
     * number. */
    CssComputedIs(style, "width", "50%");
    /* A relative length keeps its unit for the same reason. */
    CssComputedIs(style, "height", "2em");
    CssComputedIs(style, "display", "block");
    CssComputedFlagsAre(style, "display", "declared");

    /* The specified value travels with the computed one, so a caller holding
     * one half of "specified vs computed" can see the other. */
    RoboCheck(CssComputedStyleSpecifiedByName(
                   style, CssSliceFromText("color")) != NULL);
    RoboCheck(CssComputedStyleFind(
                   style, CssSliceFromText("COLOR")) != (size_t)-1);
    RoboCheck(CssComputedStyleFind(
                   style, CssSliceFromText("nope")) == (size_t)-1);

    /* `width` is declared; `padding` is not, and is *absent* rather than
     * defaulted — the property has no entry because the element said nothing
     * about it, which is a different fact from a defaulted value. */
    CssComputedIs(style, "padding", "<absent>");
    RoboCheckEqInt(CssComputedStyleHasInvalid(style), 0);
    RoboCheckEqInt((int)CssComputedStyleIncompleteCount(style), 0);
    RoboCheckEqInt(CssComputedStyleIsTruncated(style), 0);

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Inheritance, through the computed value                            */
/* ------------------------------------------------------------------ */

static void TestCssComputedInheritsComputedValue(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parent = NULL;
    CssComputedStyle *child = NULL;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    /* The parent's declaration is in `in`; the child inherits *pixels*. */
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red; font-size: 1in }");
    RoboCheckEqInt(CssComputedStyleCreate(&parent, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&child, NULL), RoboOk);

    CssComputeOf(&fixture, div, parent);
    CssComputeOf(&fixture, paragraph, child);

    CssComputedIs(parent, "font-size", "96px");
    CssComputedIs(parent, "color", "#ff0000");
    /* The child was given nothing, so it inherits — and what it inherits is the
     * parent's *computed* value. The assertion is `96px`, which the parent
     * produced from `1in`; a style that carried `1in` down would fail it. */
    CssComputedIs(child, "font-size", "96px");
    CssComputedIs(child, "color", "#ff0000");
    CssComputedFlagsAre(child, "color", "inherited");
    CssComputedFlagsAre(child, "font-size", "inherited");

    /* An inherited value is a copy: the parent's style can go away without
     * touching the child's. */
    CssComputedStyleDestroy(parent);
    parent = NULL;
    CssComputedIs(child, "color", "#ff0000");

    CssComputedStyleDestroy(child);
    CssInheritEnd(&fixture);
}

static void TestCssComputedDefaultThenInherited(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *root = NULL;
    CssComputedStyle *child = NULL;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    /* Nothing is declared anywhere: the document is a tree of defaults, and the
     * point is that a *default* is an answer. */
    RoboCheckEqInt(CssComputedStyleCreate(&root, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&child, NULL), RoboOk);

    CssComputeOf(&fixture, div, root);
    CssComputeOf(&fixture, paragraph, child);

    /* The whole inherited-by-default family is present on the root, each at the
     * registry's initial value, each normalised: `black` is a colour, `serif` is
     * a family list, `medium` is a keyword, and `text-indent`'s `0` is the one
     * bare number CSS lets a length be. */
    RoboCheckEqInt((int)CssComputedStyleCount(root), 57);
    CssComputedIs(root, "color", "#000000");
    CssComputedFlagsAre(root, "color", "default");
    CssComputedIs(root, "font-family", "serif");
    CssComputedIs(root, "font-size", "medium");
    CssComputedIs(root, "font-weight", "normal");
    CssComputedIs(root, "line-height", "normal");
    CssComputedIs(root, "text-align", "start");
    CssComputedIs(root, "text-indent", "0px");
    CssComputedIs(root, "visibility", "visible");
    CssComputedIs(root, "white-space", "normal");

    /* The child crosses the family too, and each entry says both things that are
     * true about it: the parent supplied the value and the value is a default. */
    CssComputedIs(child, "color", "#000000");
    CssComputedFlagsAre(child, "color", "inherited+default");
    CssComputedIs(child, "font-family", "serif");
    CssComputedIs(child, "text-indent", "0px");

    CssComputedStyleDestroy(child);
    CssComputedStyleDestroy(root);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Non-inherited properties                                           */
/* ------------------------------------------------------------------ */

static void TestCssComputedNotInherited(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *child = NULL;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { width: 500px; margin: 0 auto; background: blue }");
    RoboCheckEqInt(CssComputedStyleCreate(&child, NULL), RoboOk);

    CssComputeOf(&fixture, paragraph, child);

    /* None of the parent's non-inherited properties reach the child — not as a
     * missing value and not as a copied one. */
    CssComputedIs(child, "width", "<absent>");
    CssComputedIs(child, "margin", "<absent>");
    CssComputedIs(child, "background", "<absent>");

    /* And the initial value is reachable through the query door, which is where
     * a non-inherited property's "nothing declared it" answer lives. */
    CssComputeQuery(&fixture, child, "width", "auto", 1);
    CssComputeQuery(&fixture, child, "display", "inline", 1);
    CssComputeQuery(&fixture, child, "position", "static", 1);
    /* A custom property has no initial value in CSS, so there is no answer and
     * the door says so rather than inventing one. */
    {
        CssComputedValue value = RoboCssCleared;

        RoboCheckEqInt(CssStyleContextQueryComputedValue(
                              fixture.cascade.context, child,
                              CssSliceFromText("--x"), &value, NULL, NULL),
                          RoboNotFound);
    }
    /* Neither has an unknown property: nothing can say what it starts at. */
    {
        CssComputedValue value = RoboCssCleared;

        RoboCheckEqInt(CssStyleContextQueryComputedValue(
                              fixture.cascade.context, child,
                              CssSliceFromText("totally-unknown"), &value, NULL,
                              NULL),
                          RoboNotFound);
    }

    CssComputedStyleDestroy(child);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The CSS-wide keywords                                              */
/* ------------------------------------------------------------------ */

static void TestCssComputedWideKeywords(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parent = NULL;
    CssComputedStyle *initialStyle = NULL;
    CssComputedStyle *inheritStyle = NULL;
    CssComputedStyle *unsetStyle = NULL;
    CssComputedStyle *revertStyle = NULL;
    CssElementRef div;
    CssElementRef initialChild;
    CssElementRef inheritChild;
    CssElementRef unsetChild;
    CssElementRef revertChild;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    initialChild = CssHostAdd(CssInheritHost(&fixture), "p", "a", NULL);
    inheritChild = CssHostAdd(CssInheritHost(&fixture), "p", "b", NULL);
    unsetChild = CssHostAdd(CssInheritHost(&fixture), "p", "c", NULL);
    revertChild = CssHostAdd(CssInheritHost(&fixture), "p", "d", NULL);
    CssHostSetParent(CssInheritHost(&fixture), initialChild, div);
    CssHostSetParent(CssInheritHost(&fixture), inheritChild, div);
    CssHostSetParent(CssInheritHost(&fixture), unsetChild, div);
    CssHostSetParent(CssInheritHost(&fixture), revertChild, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red; width: 500px }"
                      "#a { color: initial; width: initial }"
                      "#b { color: inherit; width: inherit }"
                      "#c { color: unset; width: unset }"
                      "#d { color: revert; width: revert }");
    RoboCheckEqInt(CssComputedStyleCreate(&parent, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&initialStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&inheritStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&unsetStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&revertStyle, NULL), RoboOk);

    CssComputeOf(&fixture, div, parent);
    CssComputeOf(&fixture, initialChild, initialStyle);
    CssComputeOf(&fixture, inheritChild, inheritStyle);
    CssComputeOf(&fixture, unsetChild, unsetStyle);
    CssComputeOf(&fixture, revertChild, revertStyle);

    /* `initial` on an inherited property resets it rather than inheriting: the
     * paragraph is black, not red. */
    CssComputedIs(initialStyle, "color", "#000000");
    CssComputedFlagsAre(initialStyle, "color", "default");
    /* `initial` on a non-inherited property is the property's own start value. */
    CssComputedIs(initialStyle, "width", "auto");
    CssComputedFlagsAre(initialStyle, "width", "default");

    /* `inherit` on either kind takes the parent's computed value. */
    CssComputedIs(inheritStyle, "color", "#ff0000");
    CssComputedFlagsAre(inheritStyle, "color", "inherited");
    CssComputedIs(inheritStyle, "width", "500px");
    CssComputedFlagsAre(inheritStyle, "width", "inherited");

    /* `unset` is `inherit` for a property that inherits and `initial` for one
     * that does not — the same declaration, two different answers, which is the
     * whole point of the keyword. */
    CssComputedIs(unsetStyle, "color", "#ff0000");
    CssComputedFlagsAre(unsetStyle, "color", "inherited");
    CssComputedIs(unsetStyle, "width", "auto");
    CssComputedFlagsAre(unsetStyle, "width", "default");

    /* `revert` is recognised, carried and *reported*: obeying it means re-running
     * the cascade with an origin removed, and half-obeying it would be wrong in
     * exactly the cases it exists for. */
    CssComputedIs(revertStyle, "color", "revert");
    CssComputedFlagsAre(revertStyle, "color", "deferred+unresolved");
    RoboCheckEqInt((int)CssComputedStyleIncompleteCount(revertStyle), 2);
    RoboCheckEqInt(CssComputedStyleHasInvalid(revertStyle), 0);

    CssComputedStyleDestroy(revertStyle);
    CssComputedStyleDestroy(unsetStyle);
    CssComputedStyleDestroy(inheritStyle);
    CssComputedStyleDestroy(initialStyle);
    CssComputedStyleDestroy(parent);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Unresolved and invalid values                                      */
/* ------------------------------------------------------------------ */

static void TestCssComputedUnresolvedAndInvalid(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { width: calc(100% - 20px); background: var(--bg, red);"
                      " color: nonsense; display: block }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssComputeOf(&fixture, div, style);

    /* A value only layout can reduce is carried with its tokens, flagged as
     * needing a later stage — not rejected, and not reduced to a guess. */
    CssComputedIs(style, "width", "calc(100% - 20px)");
    CssComputedFlagsAre(style, "width", "declared+unresolved");
    RoboCheckEqInt(CssComputedStyleComputedByName(
                          style, CssSliceFromText("width"))->kind,
                      RoboCssComputedValuePreserved);

    /* A custom property reference is *substituted* at computed-value time: `--bg`
     * is undefined here, so the fallback is what the declaration computes to, and
     * the entry is no longer unresolved because nothing about it is waiting. */
    CssComputedIs(style, "background", "red");
    CssComputedFlagsAre(style, "background", "declared");

    /* A value that means nothing for its property is reported *invalid* rather
     * than dropped: the declaration was there and was wrong, and a consumer can
     * see that instead of finding the property silently absent. */
    CssComputedFlagsAre(style, "color", "declared+invalid");
    RoboCheckEqInt(CssComputedStyleHasInvalid(style), 1);
    /*
     * Three entries remain incomplete, and the third is the family 18.15 adds: the
     * reference that used to be second resolved to its fallback above, so the
     * `calc()` and the invalid colour are two, and `text-emphasis-color` is the
     * first *inherited* property in the table whose initial is a reference
     * (`currentcolor`). Its default entry on this element cannot resolve, because the
     * colour it would resolve against is the `color` this very rule declared invalid
     * — which is why the count moves by one rather than by none.
     */
    RoboCheckEqInt((int)CssComputedStyleIncompleteCount(style), 3);
    /* The invalid entry does not corrupt its neighbours: `display` is fine. */
    CssComputedIs(style, "display", "block");
    CssComputedFlagsAre(style, "display", "declared");

    /* `currentColor` is the same kind of fact: legal, carried, and *not*
     * resolved against a default. */
    {
        CssComputedStyle *second = NULL;

        RoboCheckEqInt(CssComputedStyleCreate(&second, NULL), RoboOk);
        CssInheritSheet(&fixture, RoboCssOriginAuthor, "div { color: currentColor }");
        CssComputeOf(&fixture, div, second);
        CssComputedIs(second, "color", "currentColor");
        CssComputedFlagsAre(second, "color", "declared+unresolved");
        CssComputedStyleDestroy(second);
    }

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Custom properties and inline styles                                */
/* ------------------------------------------------------------------ */

static void TestCssComputedCustomProperties(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parent = NULL;
    CssComputedStyle *child = NULL;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { --theme-color: 10px; --other: \"a string\" }");
    RoboCheckEqInt(CssComputedStyleCreate(&parent, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&child, NULL), RoboOk);

    CssComputeOf(&fixture, div, parent);
    CssComputeOf(&fixture, paragraph, child);

    /* A custom property keeps its tokens: `10px` is not turned into a length,
     * because nothing has said what the property *is*. */
    CssComputedIs(parent, "--theme-color", "10px");
    CssComputedFlagsAre(parent, "--theme-color", "declared+custom");
    RoboCheckEqInt(CssComputedStyleComputedByName(
                          parent, CssSliceFromText("--theme-color"))->kind,
                      RoboCssComputedValuePreserved);
    CssComputedIs(parent, "--other", "\"a string\"");

    /* Custom properties inherit by default (CSS says so, and only the name can
     * say it: no registry entry exists for one). */
    CssComputedIs(child, "--theme-color", "10px");
    CssComputedFlagsAre(child, "--theme-color", "inherited+custom");

    /* And the name comparison is byte-exact for them, which is the rule that
     * keeps `--Theme` from finding `--theme`. */
    RoboCheck(CssComputedStyleFind(
                   child, CssSliceFromText("--theme-color")) != (size_t)-1);
    RoboCheck(CssComputedStyleFind(
                   child, CssSliceFromText("--THEME-COLOR")) == (size_t)-1);

    CssComputedStyleDestroy(child);
    CssComputedStyleDestroy(parent);
    CssInheritEnd(&fixture);
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
typedef struct cssInlineTarget {
    CssElementRef ref;
    const char *text;
    int calls;
} CssInlineTarget;

static Result CssInlineTargetLoad(void *userdata,
                                            CssElementRef element,
                                            CssSlice *outText)
{
    CssInlineTarget *target = (CssInlineTarget *)userdata;

    target->calls++;
    if (element != target->ref) {
        return RoboNotFound;
    }
    *outText = CssSliceFromText(target->text);
    return RoboOk;
}

static void TestCssComputedInlineStyle(void)
{
    CssInheritFixture fixture;
    CssInlineTarget target;
    CssComputedStyle *parent = NULL;
    CssComputedStyle *child = NULL;
    CssComputedStyle *override = NULL;
    CssElementRef div;
    CssElementRef paragraph;
    CssElementRef own;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    own = CssHostAdd(CssInheritHost(&fixture), "p", "third", NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSetParent(CssInheritHost(&fixture), own, div);
    CssHostSeal(CssInheritHost(&fixture));

    /* The stub is standing in for the DOM: RoboCSS asks it for the `style`
     * attribute's *text* and parses it through the same grammar a rule uses, so
     * an inline declaration is not a second dialect of CSS. */
    target.ref = div;
    target.text = "color: blue; padding: 1in";
    target.calls = 0;
    RoboCheckEqInt(CssStyleContextSetInlineStyle(
                          fixture.cascade.context, CssInlineTargetLoad, &target,
                          NULL),
                      RoboOk);
    /* A rule on the second paragraph, so the child that has its own value can be
     * told apart from the one that has none. */
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "#third { color: green }");
    RoboCheckEqInt(CssComputedStyleCreate(&parent, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&child, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&override, NULL), RoboOk);

    CssComputeOf(&fixture, div, parent);
    CssComputeOf(&fixture, paragraph, child);
    CssComputeOf(&fixture, own, override);

    CssComputedIs(parent, "color", "#0000ff");
    CssComputedFlagsAre(parent, "color", "declared");
    /* A shorthand keeps its tokens: `padding: 1in` is not four longhands, and
     * expanding it would be inventing a shorthand engine. */
    CssComputedIs(parent, "padding", "1in");
    /* The first paragraph has no `style` attribute and inherits the resolved
     * inline colour. The second one's rule overrides it, because inheritance
     * never competes with a declaration on the element itself. */
    CssComputedIs(child, "color", "#0000ff");
    CssComputedFlagsAre(child, "color", "inherited");
    CssComputedIs(override, "color", "#008000");
    CssComputedFlagsAre(override, "color", "declared");
    /* The stub was asked once per element, which is what makes the inheritance
     * above a statement about an attribute rather than about a fallback. */
    RoboCheckEqInt(target.calls >= 3, 1);

    CssComputedStyleDestroy(override);
    CssComputedStyleDestroy(child);
    CssComputedStyleDestroy(parent);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The ancestor walk                                                  */
/* ------------------------------------------------------------------ */

static void TestCssComputedChain(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
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
                      "div { color: red } section { color: blue }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    /* The whole chain is computed top-down, and the paragraph gets the value of
     * its *immediate* parent — not the nearest ancestor that happened to
     * declare something. */
    CssComputeOf(&fixture, paragraph, style);
    CssComputedIs(style, "color", "#0000ff");
    CssComputedFlagsAre(style, "color", "inherited");
    /* `red` on the grandparent is nowhere in the answer: the immediate parent's
     * computed value is what an inheritance chain propagates. */
    RoboCheckEqInt(CssComputedStyleCount(style) > 0u, 1);

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestCssComputedCyclicChainIsTruncated(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef ring[4];
    size_t index;

    CssFixture();
    CssInheritBegin(&fixture);
    for (index = 0u; index < 4u; index++) {
        ring[index] = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    }
    for (index = 0u; index < 4u; index++) {
        CssHostSetParent(CssInheritHost(&fixture), ring[index],
                            ring[(index + 1u) % 4u]);
    }
    CssHostSeal(CssInheritHost(&fixture));
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    RoboCheckEqInt(CssStyleContextComputeStyle(
                          fixture.cascade.context, ring[0], style, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleIsTruncated(style), 1);
    /* No crash, no hang, and the defaults are still there. */
    CssComputedIs(style, "color", "#000000");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism and arguments                                          */
/* ------------------------------------------------------------------ */

static void TestCssComputedDeterminism(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *first = NULL;
    CssComputedStyle *second = NULL;
    CssElementRef div;
    size_t index;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "app", "panel wide");
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red; width: 10px; display: block }"
                      ".panel { color: blue; height: 50% }"
                      "#app { padding: 1px }");
    RoboCheckEqInt(CssComputedStyleCreate(&first, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&second, NULL), RoboOk);

    CssComputeOf(&fixture, div, first);
    CssComputeOf(&fixture, div, second);

    /* Same inputs, same answer — entries, order, values and flags alike. Not a
     * comparison of re-derived answers but of the two sequences. */
    RoboCheckEqInt((int)CssComputedStyleCount(first),
                      (int)CssComputedStyleCount(second));
    for (index = 0u; index < CssComputedStyleCount(first); index++) {
        CssPropertyId propertyA = RoboCssPropertyIdNone;
        CssPropertyId propertyB = RoboCssPropertyIdNone;

        RoboCheckEqInt(CssComputedStylePropertyAt(first, index, &propertyA,
                                                             NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedStylePropertyAt(second, index, &propertyB,
                                                             NULL),
                          RoboOk);
        RoboCheckEqInt((int)propertyA, (int)propertyB);
        RoboCheckEqInt((int)CssComputedStyleFlagsAt(first, index),
                          (int)CssComputedStyleFlagsAt(second, index));
        RoboCheckEqInt(CssComputedValueEquals(
                              CssComputedStyleComputedAt(first, index),
                              CssComputedStyleComputedAt(second, index)),
                          1);
    }
    /* Recomputing into a style that already holds one replaces rather than
     * appends, which is what makes a second computation on a reused object the
     * same answer as the first. */
    CssComputeOf(&fixture, div, first);
    RoboCheckEqInt((int)CssComputedStyleCount(first),
                      (int)CssComputedStyleCount(second));

    /* The cascade decided the winners before any of this: `color` is the
     * `.panel` blue and `height` is its 50%. */
    CssComputedIs(first, "color", "#0000ff");
    CssComputedIs(first, "height", "50%");
    CssComputedIs(first, "padding", "1px");

    CssComputedStyleDestroy(second);
    CssComputedStyleDestroy(first);
    CssInheritEnd(&fixture);
}

static void TestCssComputedArguments(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssError error = RoboCssCleared;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    RoboCheckEqInt(CssStyleContextComputeStyle(NULL, div, style, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStyleContextComputeStyle(fixture.cascade.context, div,
                                                           NULL, &error, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    /* "No element" is not an element, and resolving one is not an answer. */
    RoboCheckEqInt(CssStyleContextComputeStyle(
                          fixture.cascade.context, RoboCssElementRefNone, style,
                          &error, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssComputedStyleCount(style), 0u);

    /* The error record is optional, which every other door in the subsystem
     * already promises. */
    RoboCheckEqInt(CssStyleContextComputeStyle(fixture.cascade.context, div,
                                                           style, NULL, NULL),
                      RoboOk);
    RoboCheck((int)CssComputedStyleCount(style) > 0);

    /* The query door's own arguments. */
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          fixture.cascade.context, style, CssSliceEmpty(), NULL,
                          NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          NULL, style, CssSliceFromText("color"), NULL, NULL, NULL),
                      RoboInvalidArgument);
    {
        CssComputedValue value = RoboCssCleared;

        RoboCheckEqInt(CssStyleContextQueryComputedValue(
                              fixture.cascade.context, NULL,
                              CssSliceFromText("nope"), &value, NULL, NULL),
                          RoboNotFound);
    }
    /* A NULL style means "nothing was computed", and every known property still
     * answers with its initial value — which is a different fact from having no
     * answer at all. */
    CssComputeQuery(&fixture, NULL, "color", "#000000", 1);

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/**
 * A document with no stylesheets at all.
 *
 * The cascade is empty, inheritance has nothing to inherit, and the computed
 * style is still a complete answer: every inherited-by-default property at its
 * initial value and nothing else. An element with no declarations is not an
 * error, and a style that came back empty would be.
 */
static void TestCssComputedNoStylesheets(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssComputeOf(&fixture, div, style);
    RoboCheckEqInt((int)CssComputedStyleCount(style), 57);
    CssComputedIs(style, "color", "#000000");
    CssComputedIs(style, "display", "<absent>");
    RoboCheckEqInt(CssComputedStyleIsTruncated(style), 0);

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */

void TestRegisterCssComputed(void);

void TestRegisterCssComputed(void)
{
    TestRegister("css.computed.declared", TestCssComputedDeclared);
    TestRegister("css.computed.inherits_computed_value",
                       TestCssComputedInheritsComputedValue);
    TestRegister("css.computed.default_then_inherited",
                       TestCssComputedDefaultThenInherited);
    TestRegister("css.computed.not_inherited", TestCssComputedNotInherited);
    TestRegister("css.computed.wide_keywords", TestCssComputedWideKeywords);
    TestRegister("css.computed.unresolved_and_invalid",
                       TestCssComputedUnresolvedAndInvalid);
    TestRegister("css.computed.custom_properties",
                       TestCssComputedCustomProperties);
    TestRegister("css.computed.inline_style", TestCssComputedInlineStyle);
    TestRegister("css.computed.chain", TestCssComputedChain);
    TestRegister("css.computed.cyclic_chain_is_truncated",
                       TestCssComputedCyclicChainIsTruncated);
    TestRegister("css.computed.determinism", TestCssComputedDeterminism);
    TestRegister("css.computed.arguments", TestCssComputedArguments);
    TestRegister("css.computed.no_stylesheets", TestCssComputedNoStylesheets);
}
