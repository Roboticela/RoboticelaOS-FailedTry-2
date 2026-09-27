/*
 * RoboCSS conformance (Phase 9.16) — css.conformance.*
 *
 * Phase 9's other forty suites each know **one family**. Every one of them asks a
 * question it can answer alone: does `border-top-style` read back as `solid`, does
 * `padding: auto` refuse, does `background: red` reset the five longhands it does
 * not name. That is how the families were built, and it is why each of them is
 * trustworthy.
 *
 * None of them can ask the questions that are only true of the *product*, and
 * those are what this suite is:
 *
 *   the registry computes   every one of the 63 registered properties, its
 *                           initial value run through the normaliser it declares
 *                           — the one invariant that spans the whole table and
 *                           that no per-family suite can state. A property given
 *                           a shape its initial value does not fit computes to
 *                           INVALID on every element of every document, and
 *                           `css.registry.initial_values` would not notice: it
 *                           asserts the initial *shape*, which is a 9.1 fact.
 *   the matrix              one representative declaration per property, and the
 *                           exact computed value *and kind* it produces. This is
 *                           the table a refactor breaks, in one place.
 *   the document            one realistic sheet over one tree, every family
 *                           asserted on the same element.
 *   the pipeline holds      malformed CSS through the whole chain; identical bytes
 *                           producing identical styles; the build's own promises.
 *
 * What it deliberately is not: a second copy of any per-family suite. Where a
 * family's own suite already pins a rule (`css.border.style.none_trap`, the
 * `repeat-x` axes, the position pair's folding), this one does not restate it —
 * it asserts the rule *through the whole registry* or *through the whole
 * pipeline*, which is a different and weaker claim about a different thing.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* The registry computes                                               */
/* ------------------------------------------------------------------ */

/*
 * The whole table, one row per property.
 *
 * Three columns, and each is a fact no per-family suite can assert:
 *
 *   kind   what the registered initial value computes *to*. A property whose
 *          shape and whose initial value had drifted apart computes INVALID, and
 *          nothing else in the build would say so.
 *   text   the canonical spelling of that value. `margin-*` start at `0` and
 *          compute to `0px`; `color` starts at `black` and computes to the
 *          channel spelling. Both are conversions the *table* performs, and a
 *          conversion nobody asserts is a conversion that can change.
 *   flags  `declared`/`default`/`deferred`/`unresolved` on the initial value.
 *          The four border colours are the interesting row: their initial is
 *          `currentColor`, which is a *reference*, so an element that declares no
 *          border colour computes a colour that is still unresolved — and that is
 *          a fact about the product, not about the colour module.
 *
 * The table is written in the registry's own order and is asserted to reach every
 * row, so a property added later is a deliberate act here rather than a silently
 * uncovered property.
 */
typedef struct cssConformanceInitial {
    const char *name;
    const char *kind;
    const char *text;
} CssConformanceInitial;

static const CssConformanceInitial cssConformanceInitials[] = {
    /* The nine common inherited properties and their font/text relatives. */
    {"color", "colour", "#000000"},
    {"cursor", "keyword", "auto"},
    {"direction", "keyword", "ltr"},
    {"font-family", "family-list", "serif"},
    /*
     * The font family (18.14), in registry order, and the split in this block is the
     * phase's one real decision. Nine rows are closed vocabularies and compute to a
     * keyword; nine are PRESERVED declarations whose grammar is wider than any closed
     * set — a `font-stretch` percentage, a `font-variant-numeric` axis set, a
     * `font-feature-settings` tag list — and carry the author's tokens exactly as the
     * grid track templates and `border-spacing` do.
     *
     * `font-size` is the row that did **not** move: the shared length grammar already
     * carries its ten size keywords (that is why its kind is `keyword` and its text
     * `medium`), and what 18.14 adds is their classification rather than a second
     * value model. `font-style` moved in place from the bare KEYWORD shape it had had
     * since 9.7, and its kind and text are unchanged by that — which is the same
     * "shape change, not a new property" row 18.13 recorded for `direction`.
     */
    {"font-feature-settings", "preserved", "normal"},
    {"font-kerning", "keyword", "auto"},
    {"font-language-override", "preserved", "normal"},
    {"font-optical-sizing", "keyword", "auto"},
    {"font-palette", "preserved", "normal"},
    /* `medium` is a keyword and not a length: `font-size: medium` is the user
     * agent's default size, which is a different statement from a pixel value. */
    {"font-size", "keyword", "medium"},
    {"font-stretch", "preserved", "normal"},
    {"font-style", "keyword", "normal"},
    /* The four `font-synthesis-*` longhands share one shape because they take the
     * same two values, and the shorthand above them is carried like every shorthand
     * here. */
    {"font-synthesis", "preserved", "auto auto auto auto"},
    {"font-synthesis-position", "keyword", "auto"},
    {"font-synthesis-small-caps", "keyword", "auto"},
    {"font-synthesis-style", "keyword", "auto"},
    {"font-synthesis-weight", "keyword", "auto"},
    {"font-variant", "keyword", "normal"},
    {"font-variant-alternates", "preserved", "normal"},
    {"font-variant-caps", "keyword", "normal"},
    {"font-variant-east-asian", "preserved", "normal"},
    /* The one row of the family that is carried *without* a vocabulary in RoboCSS:
     * its five ligature tags and their policy are RoboText's (12.12), and a second
     * copy here is how the shape a CSS author writes and the policy a shaper reads
     * would come to disagree. */
    {"font-variant-ligatures", "preserved", "normal"},
    {"font-variant-numeric", "preserved", "normal"},
    {"font-variant-position", "keyword", "normal"},
    {"font-variation-settings", "preserved", "normal"},
    {"font-weight", "keyword", "normal"},
    /*
     * The text family (18.15), in registry order. Ten rows are closed vocabularies
     * and compute to a keyword; the rest are PRESERVED declarations whose grammar is
     * wider than any closed set — a `text-decoration-line` set, a `text-overflow`
     * string, a `text-decoration-thickness` length — and carry the author's tokens
     * exactly as the grid track templates and `border-spacing` do.
     *
     * `text-align`, `text-transform` and `white-space` moved in place from the bare
     * KEYWORD shape they had had since 9.7, and their kind and text are unchanged by
     * that — the same "shape change, not a new property" row 18.13 recorded for
     * `direction` and 18.14 for `font-style`. `text-emphasis-color` is the family's
     * one *inherited* reference initial (`currentcolor`), so its computed text is the
     * colour module's canonical spelling while its sibling `text-emphasis` carries the
     * same word as a preserved token, exactly as `border` and `border-color` differ.
     */
    {"hyphens", "keyword", "manual"},
    /*
     * The image family (18.16), in registry order. How an image is rendered, how a
     * file's own orientation is read and its resolution are settings of the element
     * that holds the image, so these three inherit; fitting the content to a box is
     * the box's own business and is in the non-inherited block below.
     *
     * `image-rendering` computes to its keyword; `image-orientation` is carried because
     * its grammar is `from-image | none | <angle>` and the angle has no keyword, and
     * `image-resolution` is carried because its grammar is a `||` of two optional
     * parts. `from-image` is the family's `from-font` — a fact about the *file* rather
     * than a policy. */
    {"image-orientation", "preserved", "from-image"},
    {"image-rendering", "keyword", "auto"},
    {"image-resolution", "preserved", "1dppx"},
    {"letter-spacing", "keyword", "normal"},
    {"line-break", "keyword", "auto"},
    {"line-height", "keyword", "normal"},
    {"overflow-wrap", "keyword", "normal"},
    {"tab-size", "preserved", "8"},
    {"text-align", "keyword", "start"},
    {"text-align-last", "keyword", "auto"},
    {"text-emphasis", "preserved", "none currentcolor"},
    {"text-emphasis-color", "colour", "currentColor"},
    {"text-emphasis-position", "preserved", "over right"},
    {"text-emphasis-style", "preserved", "none"},
    /* A bare `0` is CSS's one number that *is* a length, and it arrives as `0px`. */
    {"text-indent", "length", "0px"},
    {"text-justify", "keyword", "auto"},
    {"text-shadow", "preserved", "none"},
    {"text-transform", "keyword", "none"},
    {"text-underline-offset", "preserved", "auto"},
    {"text-underline-position", "preserved", "auto"},
    {"word-break", "keyword", "normal"},
    {"visibility", "keyword", "visible"},
    {"white-space", "keyword", "normal"},
    {"word-spacing", "keyword", "normal"},
    /* The background family. The shorthand carries its tokens — a shorthand's own
     * computed value is PRESERVED and the distribution is what types it. */
    {"background", "preserved", "transparent"},
    {"background-color", "colour", "#00000000"},
    {"background-image", "keyword", "none"},
    {"background-position", "background-position", "0% 0%"},
    {"background-size", "background-size", "auto"},
    {"background-repeat", "keyword", "repeat"},
    {"background-attachment", "keyword", "scroll"},
    /*
     * 18.17 completes the background family: the box vocabulary `background-clip`
     * and `background-origin` share (and differ only in their initial value), and the
     * sixteen blend modes. All three are non-inherited like the six above them. */
    {"background-blend-mode", "keyword", "normal"},
    {"background-clip", "keyword", "border-box"},
    {"background-origin", "keyword", "padding-box"},
    /* The four border widths, and `medium` again — a keyword, not three pixels. */
    {"border-bottom-width", "keyword", "medium"},
    {"border-left-width", "keyword", "medium"},
    {"border-right-width", "keyword", "medium"},
    {"border-top-width", "keyword", "medium"},
    /*
     * The border shorthands and their side forms are PRESERVED; their three
     * longhands each are not.
     *
     * The two spellings here are the whole point of the difference. A PRESERVED
     * value is the tokens the registry wrote — `medium none currentcolor` — while
     * a COLOUR value is the colour module's canonical spelling, `currentColor`.
     * So the same word appears one way in the first five rows and the other way
     * in the six colour rows below, and a build that normalised a shorthand's
     * tokens or preserved a colour's would show up as exactly this diff.
     */
    {"border", "preserved", "medium none currentcolor"},
    {"border-bottom", "preserved", "medium none currentcolor"},
    {"border-bottom-color", "colour", "currentColor"},
    {"border-bottom-style", "border-style", "none"},
    {"border-color", "preserved", "currentcolor"},
    {"border-left", "preserved", "medium none currentcolor"},
    {"border-left-color", "colour", "currentColor"},
    {"border-left-style", "border-style", "none"},
    {"border-right", "preserved", "medium none currentcolor"},
    {"border-right-color", "colour", "currentColor"},
    {"border-right-style", "border-style", "none"},
    {"border-style", "preserved", "none"},
    {"border-top", "preserved", "medium none currentcolor"},
    {"border-top-color", "colour", "currentColor"},
    {"border-top-style", "border-style", "none"},
    {"border-width", "preserved", "medium"},
    /*
     * 18.18 completes the border family: the border-image decorations, the outline
     * family and `border-radius` with its four corners. Only `border-image-repeat`
     * takes a closed vocabulary; `border-image-source` reuses the image shape (so its
     * `none` computes to a keyword) and the rest are carried. `outline-style` reuses
     * the border style shape, `outline-width` the shared line width and
     * `outline-color` the colour — so their kinds are the border family's own. */
    {"border-image", "preserved", "none"},
    {"border-image-outset", "preserved", "0"},
    {"border-image-repeat", "keyword", "stretch"},
    {"border-image-slice", "preserved", "100%"},
    {"border-image-source", "keyword", "none"},
    {"border-image-width", "preserved", "1"},
    {"border-radius", "preserved", "0"},
    {"border-top-left-radius", "preserved", "0"},
    {"border-top-right-radius", "preserved", "0"},
    {"border-bottom-right-radius", "preserved", "0"},
    {"border-bottom-left-radius", "preserved", "0"},
    {"outline", "preserved", "medium none currentcolor"},
    {"outline-color", "colour", "currentColor"},
    {"outline-offset", "preserved", "0"},
    {"outline-style", "border-style", "none"},
    {"outline-width", "keyword", "medium"},
    /* Positioning: an inset starts at `auto` and `z-index` at `auto`, neither of
     * which is zero — the one pair of initial values whose wrong answer would
     * look entirely plausible. */
    {"bottom", "keyword", "auto"},
    {"box-sizing", "keyword", "content-box"},
    {"display", "display", "inline"},
    /* `font` is the family's one non-inherited row, and its carried value is the
     * concatenation of its seven longhands' initials — the same construction
     * `border` records as `medium none currentcolor`. */
    {"font", "preserved", "normal normal normal normal medium/normal serif"},
    {"height", "keyword", "auto"},
    {"left", "keyword", "auto"},
    /* The box model's eight edges: `margin`/`padding` are shorthands (PRESERVED)
     * and their eight longhands are typed. */
    {"margin", "preserved", "0"},
    {"margin-bottom", "length", "0px"},
    {"margin-left", "length", "0px"},
    {"margin-right", "length", "0px"},
    {"margin-top", "length", "0px"},
    /*
     * The non-inherited half of the image family (18.16): a replaced element's fitting
     * belongs to its box, not to its content. `object-fit` takes the closed shape, and
     * `object-position` is carried because it is the position *pair* the background
     * module owns rather than a second copy of that grammar. */
    {"object-fit", "keyword", "fill"},
    {"object-position", "preserved", "50% 50%"},
    /* Opacity (11.9): the row a *paint* phase registered, and the only one here whose
     * initial value is not the zero of its own vocabulary -- so its computed text is `1`
     * where every property around it computes to an initial spelling of nothing-ness. */
    {"opacity", "opacity", "1"},
    /* Overflow (10.13): the family a *layout* phase registered, and the one whose
     * shorthand takes the vocabulary rather than staying PRESERVED — because no phase
     * in this build expands a shorthand, so a token list here would be a declaration
     * nothing reads. */
    {"overflow", "overflow", "visible"},
    {"overflow-x", "overflow", "visible"},
    {"overflow-y", "overflow", "visible"},
    {"padding", "preserved", "0"},
    {"padding-bottom", "length", "0px"},
    {"padding-left", "length", "0px"},
    {"padding-right", "length", "0px"},
    {"padding-top", "length", "0px"},
    {"position", "position", "static"},
    {"right", "keyword", "auto"},
    /* The non-inherited half of the text family (18.15): a decoration is painted per
     * element, so a child of an underlined link is not underlined, and truncation
     * belongs to the box that clips rather than to the text inside it. The shorthand
     * carries the concatenation of its four longhands' initials, as `border` does. */
    {"text-decoration", "preserved", "none currentcolor solid auto"},
    {"text-decoration-color", "colour", "currentColor"},
    {"text-decoration-line", "preserved", "none"},
    {"text-decoration-style", "keyword", "solid"},
    {"text-decoration-thickness", "preserved", "auto"},
    {"text-overflow", "preserved", "clip"},
    {"top", "keyword", "auto"},
    {"width", "keyword", "auto"},
    {"z-index", "keyword", "auto"},
    /* Flexbox (18.3): the family whose values are three vocabularies. `normal` is the
     * alignment initial for three of the four properties and `auto` for `align-self`,
     * and `flex-shrink` starts at `1` while `flex-grow` starts at `0` -- the two
     * initial values here a cleared record would get wrong. The two shorthands are
     * PRESERVED and carry the concatenation of their halves. */
    {"align-content", "flex-align", "normal"},
    {"align-items", "flex-align", "normal"},
    {"align-self", "flex-align", "auto"},
    {"flex", "preserved", "0 1 auto"},
    {"flex-basis", "keyword", "auto"},
    {"flex-direction", "flex-direction", "row"},
    {"flex-flow", "preserved", "row nowrap"},
    {"flex-grow", "number", "0"},
    {"flex-shrink", "number", "1"},
    {"flex-wrap", "flex-wrap", "nowrap"},
    {"justify-content", "flex-align", "normal"},
    {"order", "number", "0"},
    /* Grid (18.4): one typed flow vocabulary, one gutter length family, and the
     * track/placement properties carried as PRESERVED declarations -- the phase
     * states that boundary rather than reading a track list. `grid-auto-flow`
     * prints its two-word spellings with the one canonical space. */
    {"column-gap", "keyword", "normal"},
    {"gap", "preserved", "normal"},
    {"grid", "preserved", "none"},
    {"grid-area", "preserved", "auto"},
    {"grid-auto-columns", "preserved", "auto"},
    {"grid-auto-flow", "grid-auto-flow", "row"},
    {"grid-auto-rows", "preserved", "auto"},
    {"grid-column", "preserved", "auto"},
    {"grid-column-end", "preserved", "auto"},
    {"grid-column-gap", "keyword", "normal"},
    {"grid-column-start", "preserved", "auto"},
    {"grid-row", "preserved", "auto"},
    {"grid-row-end", "preserved", "auto"},
    {"grid-row-gap", "keyword", "normal"},
    {"grid-row-start", "preserved", "auto"},
    {"grid-template", "preserved", "none"},
    {"grid-template-areas", "preserved", "none"},
    {"grid-template-columns", "preserved", "none"},
    {"grid-template-rows", "preserved", "none"},
    {"row-gap", "keyword", "normal"},
    /* The table family (18.5), in registry order. Five of its six rows are closed
     * vocabularies and the sixth is carried: `border-spacing`'s `<length>{1,2}` pair
     * is a declaration this build holds rather than reads, exactly as the grid track
     * templates are. */
    {"border-collapse", "keyword", "separate"},
    {"border-spacing", "preserved", "0"},
    {"caption-side", "keyword", "top"},
    {"empty-cells", "keyword", "show"},
    {"table-layout", "keyword", "auto"},
    /* The multi-column family (18.6), in registry order. The count and width are
     * typed, the span and fill are carried keywords, the rule decoration reuses the
     * border family's shapes, and the two shorthands are carried. */
    {"column-count", "keyword", "auto"},
    {"column-fill", "keyword", "balance"},
    {"column-rule", "preserved", "medium none currentColor"},
    {"column-rule-color", "colour", "currentColor"},
    {"column-rule-style", "border-style", "none"},
    {"column-rule-width", "keyword", "medium"},
    {"column-span", "keyword", "none"},
    {"column-width", "keyword", "auto"},
    {"columns", "preserved", "auto auto"},
    /* The writing-mode family (18.13), in registry order. `direction` above is the
     * row that already existed and whose shape was upgraded in place; its computed
     * *kind* is still a keyword, which is why its row did not change. The two new
     * rows are inherited keywords, and `horizontal-tb`/`mixed` are each the zero of
     * their enumerator. */
    {"writing-mode", "keyword", "horizontal-tb"},
    {"text-orientation", "keyword", "mixed"},
    /* The transform family (18.7), in registry order. The two typed vocabularies
     * compute to a keyword; the seven carried declarations keep their tokens. */
    {"transform", "preserved", "none"},
    {"transform-origin", "preserved", "50% 50%"},
    {"transform-style", "keyword", "flat"},
    {"perspective", "preserved", "none"},
    {"perspective-origin", "preserved", "50% 50%"},
    {"backface-visibility", "keyword", "visible"},
    {"translate", "preserved", "none"},
    {"rotate", "preserved", "none"},
    {"scale", "preserved", "none"},
    /* The transition family (18.8), in registry order. One typed keyword and five
     * carried grammars. */
    {"transition-behavior", "keyword", "normal"},
    {"transition-delay", "preserved", "0s"},
    {"transition-duration", "preserved", "0s"},
    {"transition-property", "preserved", "all"},
    {"transition-timing-function", "preserved", "ease"},
    {"transition", "preserved", "all 0s ease 0s"},
    /* The animation family (18.9), in registry order. Four typed vocabularies, the
     * iteration-count shape (`1` is a number) and the five carried grammars. */
    {"animation", "preserved",
     "none 0s ease 0s 1 normal none running"},
    {"animation-composition", "keyword", "replace"},
    {"animation-delay", "preserved", "0s"},
    {"animation-direction", "keyword", "normal"},
    {"animation-duration", "preserved", "0s"},
    {"animation-fill-mode", "keyword", "none"},
    {"animation-iteration-count", "number", "1"},
    {"animation-name", "preserved", "none"},
    {"animation-play-state", "keyword", "running"},
    {"animation-timing-function", "preserved", "ease"},
    /* The container-query family (18.11), in registry order. */
    {"container-type", "keyword", "normal"},
    {"container-name", "preserved", "none"},
    {"container", "preserved", "none / normal"},
    /* The logical family (18.12), in registry order. The logical longhands compute
     * exactly as their physical counterparts do. */
    {"margin-block-start", "length", "0px"},
    {"margin-block-end", "length", "0px"},
    {"margin-inline-start", "length", "0px"},
    {"margin-inline-end", "length", "0px"},
    {"padding-block-start", "length", "0px"},
    {"padding-block-end", "length", "0px"},
    {"padding-inline-start", "length", "0px"},
    {"padding-inline-end", "length", "0px"},
    {"inset-block-start", "keyword", "auto"},
    {"inset-block-end", "keyword", "auto"},
    {"inset-inline-start", "keyword", "auto"},
    {"inset-inline-end", "keyword", "auto"},
    {"block-size", "keyword", "auto"},
    {"inline-size", "keyword", "auto"},
    {"margin-block", "preserved", "0"},
    {"margin-inline", "preserved", "0"},
    {"padding-block", "preserved", "0"},
    {"padding-inline", "preserved", "0"},
    /* 18.19's two rows: the carried filter lists. `none` is a value, not an empty
     * list. */
    {"filter", "preserved", "none"},
    {"backdrop-filter", "preserved", "none"}
};

/**
 * Every registered property's initial value computes to a valid, typed value.
 *
 * This is the invariant that spans the whole table, and the one a per-family
 * suite cannot state: it is not about a family, it is about the *registry* and
 * the normaliser agreeing. A row whose `computation` does not describe its
 * `initialText` — a `display` shape on `border-width`, a `colour` shape on
 * `padding` — computes INVALID here and nowhere else.
 *
 * The kind, the canonical text and the row's own identity are asserted together,
 * because a test that checked only "not INVALID" would pass for a property that
 * had quietly been given another property's shape.
 */
static void TestCssConformanceRegistryEveryInitialComputes(void)
{
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    RoboCheck(registry != NULL);

    /*
     * The table is the coverage, so it is asserted to be the whole registry before
     * it is walked. A property added to the registry without a row here fails
     * *this* line rather than silently going untested.
     */
    RoboCheckEqInt((int)(sizeof(cssConformanceInitials) /
                            sizeof(cssConformanceInitials[0])),
                      (int)CssPropertyRegistryCount(registry));

    for (index = 0u; index < CssPropertyRegistryCount(registry); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryAt(registry, index);
        const CssValue *initial;
        CssComputedValue value = RoboCssCleared;
        char text[RoboCssComputedValueTextMax];
        size_t length = 0u;

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        /*
         * The table is in the registry's order, so a row that had drifted is
         * caught by name rather than by a count that happened to still agree.
         */
        if (index < sizeof(cssConformanceInitials) / sizeof(cssConformanceInitials[0])) {
            RoboCheckStrEq(definition->name, cssConformanceInitials[index].name);
        }
        /* Every row declares a computation, and it is one this build knows. */
        RoboCheckEqInt(CssPropertyComputationIsKnown(definition->computation), 1);
        /* Non-zero because it is a real enumerator, not the sentinel. */
        RoboCheck(CssPropertyComputationIsKnown(definition->computation) != 0);

        initial = CssPropertyDefinitionInitialValue(definition);
        RoboCheck(initial != NULL);
        if (!initial) {
            continue;
        }
        /* RoboOk even for an INVALID answer: the call succeeding and the value
         * meaning nothing are two different facts, and this asserts the first. */
        RoboCheckEqInt(CssComputedValueCompute(definition->computation, initial,
                                                         &value, NULL),
                          RoboOk);
        /* The answer itself. INVALID is an answer, and the wrong one here. */
        RoboCheck(value.kind != RoboCssComputedValueInvalid);
        RoboCheck(value.kind != RoboCssComputedValueNone);
        RoboCheckEqInt(CssComputedValueKindIsKnown(value.kind), 1);
        RoboCheckStrEq(CssComputedValueKindName(value.kind),
                          index < sizeof(cssConformanceInitials) /
                                      sizeof(cssConformanceInitials[0])
                              ? cssConformanceInitials[index].kind
                              : "unknown");
        text[0] = '\0';
        RoboCheckEqInt(CssComputedValueWrite(&value, text, sizeof(text), &length,
                                                        NULL),
                          RoboOk);
        RoboCheckStrEq(text, index < sizeof(cssConformanceInitials) /
                                    sizeof(cssConformanceInitials[0])
                                ? cssConformanceInitials[index].text
                                : "");
        CssComputedValueDispose(&value);
    }

    CssPropertyRegistryDestroy(registry);
}

/**
 * The four border colours compute to a *reference*, and the one place that shows.
 *
 * A border nothing declared has `currentColor` as its colour, and `currentColor`
 * is the registry's only initial value that is a reference rather than a value.
 * So the computed record carries a contextual colour and is flagged unresolved —
 * which is the fact that keeps a consumer from reading a border's colour as
 * black before anything has resolved it.
 *
 * `background-color`'s initial is `transparent`, which is a *colour*, so it is
 * the control: same shape, same layer, and not unresolved.
 */
static void TestCssConformanceRegistryReferenceInitial(void)
{
    CssPropertyRegistry *registry = NULL;
    static const char *const references[] = {
        "border-top-color", "border-right-color", "border-bottom-color",
        "border-left-color"
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);

    for (index = 0u; index < sizeof(references) / sizeof(references[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, references[index]);
        CssComputedValue value = RoboCssCleared;
        char text[RoboCssComputedValueTextMax];
        size_t length = 0u;

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(CssComputedValueCompute(
                              definition->computation,
                              CssPropertyDefinitionInitialValue(definition), &value,
                              NULL),
                          RoboOk);
        RoboCheckEqInt(value.kind, RoboCssComputedValueColor);
        /* The two facts together, because either alone would be ambiguous: a
         * resolved `currentColor` and an unresolved one are the same kind. */
        RoboCheckEqInt(value.unresolved, 1);
        RoboCheckEqInt(CssColorIsCurrent(value.color), 1);
        text[0] = '\0';
        RoboCheckEqInt(CssComputedValueWrite(&value, text, sizeof(text), &length,
                                                        NULL),
                          RoboOk);
        RoboCheckStrEq(text, "currentColor");
        CssComputedValueDispose(&value);
    }

    /* The control: a colour-shaped initial that is not a reference. */
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "background-color");
        CssComputedValue value = RoboCssCleared;

        RoboCheck(definition != NULL);
        RoboCheckEqInt(CssComputedValueCompute(
                              definition->computation,
                              CssPropertyDefinitionInitialValue(definition), &value,
                              NULL),
                          RoboOk);
        RoboCheckEqInt(value.kind, RoboCssComputedValueColor);
        RoboCheckEqInt(value.unresolved, 0);
        RoboCheckEqInt(CssColorIsCurrent(value.color), 0);
        CssComputedValueDispose(&value);
    }

    CssPropertyRegistryDestroy(registry);
}

/**
 * Every computation a registry row declares is one the normaliser knows.
 *
 * The enumerator's own contract, over the whole table rather than over the ten a
 * phase happened to add: a row carrying an enumerator the normaliser's switch
 * does not handle reaches its `default` and produces INVALID for every element
 * that declares the property — including the elements that declared it correctly.
 */
static void TestCssConformanceRegistryComputationsAreKnown(void)
{
    CssPropertyRegistry *registry = NULL;
    size_t index;
    size_t known = 0u;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);

    for (index = 0u; index < CssPropertyRegistryCount(registry); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryAt(registry, index);

        if (!definition) {
            continue;
        }
        /* Both doors, because they are two: the predicate and the name table. */
        if (CssPropertyComputationIsKnown(definition->computation)) {
            known++;
            RoboCheck(CssPropertyComputationName(definition->computation) != NULL);
            RoboCheck(CssPropertyComputationName(definition->computation)[0] != '\0');
            /* And a *named* shape, never the fallback. */
            RoboCheck(CssPropertyComputationName(definition->computation)[0] != 'u' ||
                       strcmp(CssPropertyComputationName(definition->computation),
                              "unknown") != 0);
        }
    }
    /* Every row, so a name table that had lost a case would show here as a count. */
    RoboCheckEqInt((int)known, (int)CssPropertyRegistryCount(registry));

    /* The vocabulary is closed and bounded, walked in enumerator order: an
     * enumerator beyond COUNT is not one, and COUNT itself is not one either. */
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          (CssPropertyComputation)RoboCssPropertyComputationCount),
                      0);

    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* The declaration matrix                                              */
/* ------------------------------------------------------------------ */

/*
 * One representative declaration per supported property, with the computed value
 * it must produce.
 *
 * The registry's initial values are one document; these are the *declarations*,
 * which is what an author writes. Both columns matter: the text is what a
 * consumer prints, and the kind is what a consumer dispatches on. `width: 50%`
 * printing as `50%` and being a PERCENTAGE rather than a LENGTH is the whole of
 * 9.10's contract, and it is only visible when both are asserted.
 */
typedef struct cssConformanceDeclaration {
    const char *property;
    const char *declaration;
    const char *kind;
    const char *text;
} CssConformanceDeclaration;

static const CssConformanceDeclaration cssConformanceDeclarations[] = {
    /* Colours (9.9) — every syntax family through the property that owns it. */
    {"color", "color: red", "colour", "#ff0000"},
    {"color", "color: #336699", "colour", "#336699"},
    {"color", "color: rgb(1, 2, 3)", "colour", "#010203"},
    {"color", "color: hsl(120, 100%, 50%)", "colour", "#00ff00"},
    {"color", "color: transparent", "colour", "#00000000"},
    {"color", "color: rgb(0, 0, 0, 0.5)", "colour", "#00000080"},
    {"color", "color: currentColor", "colour", "currentColor"},
    {"color", "color: oklch(0.5 0.1 200)", "preserved", "oklch(0.5 0.1 200)"},
    {"color", "color: banana", "invalid", "unknown colour name"},
    /* Numbers and units (9.10). */
    {"width", "width: 10px", "length", "10px"},
    {"width", "width: 50%", "percentage", "50%"},
    {"width", "width: 0", "length", "0px"},
    {"width", "width: 1in", "length", "96px"},
    {"width", "width: 2em", "length", "2em"},
    {"width", "width: 10vw", "length", "10vw"},
    {"width", "width: auto", "keyword", "auto"},
    {"width", "width: calc(100% - 20px)", "preserved", "calc(100% - 20px)"},
    {"width", "width: 10", "invalid", "a bare number is not a length"},
    {"width", "width: 10foo", "invalid", "unknown length unit"},
    {"line-height", "line-height: 1.5", "number", "1.5"},
    {"line-height", "line-height: 150%", "percentage", "150%"},
    {"z-index", "z-index: 0", "number", "0"},
    {"z-index", "z-index: -3", "number", "-3"},
    {"z-index", "z-index: auto", "keyword", "auto"},
    {"z-index", "z-index: 1.5", "invalid", "not a whole number"},
    /* The box model (9.11): both refusals, and the two typed edges. */
    {"margin-top", "margin-top: 10px", "length", "10px"},
    {"margin-top", "margin-top: 50%", "percentage", "50%"},
    {"margin-top", "margin-top: auto", "keyword", "auto"},
    {"padding-top", "padding-top: 10px", "length", "10px"},
    {"padding-top", "padding-top: auto", "invalid", "not a padding length"},
    /* The overflow family (10.13) — both values, both refusals, and the one
     * declaration shape the vocabulary deliberately does not read. */
    {"overflow-x", "overflow-x: hidden", "overflow", "hidden"},
    {"overflow-y", "overflow-y: visible", "overflow", "visible"},
    {"overflow", "overflow: hidden", "overflow", "hidden"},
    {"overflow", "overflow: banana", "invalid", "not an overflow value"},
    {"overflow", "overflow: 20px", "invalid", "not an identifier"},
    {"border-top-width", "border-top-width: 2px", "length", "2px"},
    {"border-top-width", "border-top-width: thin", "keyword", "thin"},
    /* Display (9.12) — the closed vocabulary, and the one that is not a keyword. */
    {"display", "display: none", "display", "none"},
    {"display", "display: block", "display", "block"},
    {"display", "display: inline", "display", "inline"},
    {"display", "display: inline-block", "display", "inline-block"},
    {"display", "display: bogus", "invalid", "not a display value"},
    {"display", "display: 20px", "invalid", "not an identifier"},
    /* Positioning (9.13). */
    {"position", "position: relative", "position", "relative"},
    {"position", "position: sticky", "position", "sticky"},
    {"position", "position: banana", "invalid", "not a position value"},
    {"top", "top: 25%", "percentage", "25%"},
    {"top", "top: auto", "keyword", "auto"},
    {"top", "top: 10px", "length", "10px"},
    /* Backgrounds (9.14). */
    {"background-color", "background-color: #ffffff", "colour", "#ffffff"},
    {"background-image", "background-image: none", "keyword", "none"},
    {"background-image", "background-image: url(\"a.png\")", "preserved",
     "url(\"a.png\")"},
    {"background-repeat", "background-repeat: no-repeat", "keyword", "no-repeat"},
    {"background-repeat", "background-repeat: repeat-x", "keyword", "repeat-x"},
    {"background-position", "background-position: center", "background-position",
     "center center"},
    {"background-position", "background-position: 25% 50%", "background-position",
     "25% 50%"},
    {"background-size", "background-size: cover", "background-size", "cover"},
    {"background-size", "background-size: 100px 50px", "background-size", "100px 50px"},
    {"background-attachment", "background-attachment: fixed", "keyword", "fixed"},
    /* 18.17: the two box rows, the blend mode, and the values 9.14's §13 named as not
     * built — `space`/`round` and `local` — now read through the same table. */
    {"background-repeat", "background-repeat: space", "keyword", "space"},
    {"background-repeat", "background-repeat: round", "keyword", "round"},
    {"background-attachment", "background-attachment: local", "keyword", "local"},
    {"background-clip", "background-clip: content-box", "keyword", "content-box"},
    {"background-clip", "background-clip: banana", "invalid",
     "not a background box value"},
    {"background-origin", "background-origin: border-box", "keyword", "border-box"},
    {"background-blend-mode", "background-blend-mode: multiply", "keyword",
     "multiply"},
    {"background-blend-mode", "background-blend-mode: banana", "invalid",
     "not a background blend mode value"},
    {"background-repeat", "background-repeat: banana", "invalid",
     "not a background repeat value"},
    /* Borders (9.15) — the shape 9.15 added, and the one whose `none` is a value. */
    {"border-top-style", "border-top-style: solid", "border-style", "solid"},
    {"border-top-style", "border-top-style: none", "border-style", "none"},
    {"border-top-style", "border-top-style: hidden", "border-style", "hidden"},
    {"border-top-style", "border-top-style: banana", "invalid",
     "not a border style value"},
    {"border-top-color", "border-top-color: #00ff00", "colour", "#00ff00"},
    /* 18.18: the one closed set among the sixteen rows, one refusal for it, and the
     * carried/reused half — an image source, an outline and a radius all survive. */
    {"border-image-repeat", "border-image-repeat: round", "keyword", "round"},
    {"border-image-repeat", "border-image-repeat: banana", "invalid",
     "not a border-image repeat value"},
    {"border-image-source", "border-image-source: url(b.png)", "preserved",
     "url(b.png)"},
    {"border-image-slice", "border-image-slice: 30% fill", "preserved",
     "30% fill"},
    {"outline-style", "outline-style: dashed", "border-style", "dashed"},
    {"outline-width", "outline-width: thick", "keyword", "thick"},
    /* 18.19: the filter lists are carried whole -- order included. */
    {"filter", "filter: blur(2px) grayscale(100%)", "preserved",
     "blur(2px) grayscale(100%)"},
    {"filter", "filter: none", "preserved", "none"},
    {"backdrop-filter", "backdrop-filter: brightness(0.5)", "preserved",
     "brightness(0.5)"},
    {"outline-offset", "outline-offset: 2px", "preserved", "2px"},
    {"border-radius", "border-radius: 10px", "preserved", "10px"},
    {"border-top-left-radius", "border-top-left-radius: 5px 8px", "preserved",
     "5px 8px"},
    /* The font / text properties, so the matrix is the whole surface and not the
     * half Phase 9's later goals happen to have touched. */
    {"font-family", "font-family: Georgia, serif", "family-list", "Georgia, serif"},
    {"font-weight", "font-weight: bold", "keyword", "bold"},
    {"font-weight", "font-weight: 700", "number", "700"},
    {"letter-spacing", "letter-spacing: 2px", "length", "2px"},
    {"visibility", "visibility: hidden", "keyword", "hidden"},
    {"box-sizing", "box-sizing: border-box", "keyword", "border-box"},
    /* The text family (18.15): the ten closed vocabularies, one refusal per kind,
     * and the carried half whose grammar is wider than any closed set — a decoration
     * line set, an overflow marker and a thickness length all survive intact. */
    {"text-align", "text-align: center", "keyword", "center"},
    {"text-align", "text-align: middle", "invalid", "not a text-align value"},
    {"text-align-last", "text-align-last: justify", "keyword", "justify"},
    {"text-justify", "text-justify: inter-character", "keyword",
     "inter-character"},
    {"text-transform", "text-transform: uppercase", "keyword", "uppercase"},
    {"white-space", "white-space: break-spaces", "keyword", "break-spaces"},
    {"white-space", "white-space: banana", "invalid", "not a white-space value"},
    {"word-break", "word-break: keep-all", "keyword", "keep-all"},
    {"overflow-wrap", "overflow-wrap: anywhere", "keyword", "anywhere"},
    {"line-break", "line-break: strict", "keyword", "strict"},
    {"hyphens", "hyphens: auto", "keyword", "auto"},
    /* The image family (18.16): the two closed vocabularies, one refusal each, and the
     * carried half whose grammar is wider than any closed set — an orientation angle,
     * a resolution and a position pair all survive intact. */
    {"image-rendering", "image-rendering: pixelated", "keyword", "pixelated"},
    {"image-rendering", "image-rendering: banana", "invalid",
     "not an image-rendering value"},
    {"object-fit", "object-fit: scale-down", "keyword", "scale-down"},
    {"object-fit", "object-fit: middle", "invalid", "not an object-fit value"},
    {"image-orientation", "image-orientation: 90deg", "preserved", "90deg"},
    {"image-resolution", "image-resolution: 2dppx", "preserved", "2dppx"},
    {"object-position", "object-position: 25% 75%", "preserved", "25% 75%"},
    {"text-decoration-style", "text-decoration-style: wavy", "keyword", "wavy"},
    {"text-decoration-style", "text-decoration-style: banana", "invalid",
     "not a text-decoration-style value"},
    {"text-decoration-line", "text-decoration-line: underline overline",
     "preserved", "underline overline"},
    {"text-overflow", "text-overflow: ellipsis", "preserved", "ellipsis"},
    {"text-decoration-thickness", "text-decoration-thickness: 3px",
     "preserved", "3px"},
    /*
     * Opacity (11.9's prerequisite) — the one property whose value is a **range** rather
     * than a closed set, so the three shapes worth pinning are its number, its
     * percentage, and the clamp that makes `150%` an answer rather than a refusal.
     * `opacity: 2` and `opacity: -1` are in the dedicated suite (`css.opacity.*`); these
     * three are here because the matrix is the whole surface and not the half of it that
     * happens to own a syntax family.
     */
    {"opacity", "opacity: 0.5", "opacity", "0.5"},
    {"opacity", "opacity: 150%", "opacity", "1"},
    {"opacity", "opacity: banana", "invalid", "not a number or a percentage"}
};

/** A shape string for a computed value's diagnostic text. */
static void CssConformanceShapeOf(CssPropertyRegistry *registry,
                                     const CssConformanceDeclaration *row,
                                     char *out,
                                     size_t capacity,
                                     const char **outKind)
{
    const CssPropertyDefinition *definition =
        CssPropertyRegistryFind(registry, row->property);
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    const CssDeclaration *declaration;
    CssComputedValue value = RoboCssCleared;
    size_t length = 0u;

    out[0] = '\0';
    *outKind = "<no property>";
    RoboCheck(definition != NULL);
    if (!definition) {
        return;
    }
    /* Through the *real* declaration grammar, so the value under test is one the
     * tokenizer and parser produced rather than a hand-built tree. */
    RoboCheckEqInt(CssParseDeclarations(row->declaration, &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    if (declaration) {
        RoboCheckEqInt(CssComputedValueCompute(
                              definition->computation,
                              CssDeclarationValue(declaration), &value, NULL),
                          RoboOk);
        *outKind = CssComputedValueKindName(value.kind);
        if (value.kind == RoboCssComputedValueInvalid) {
            /* An INVALID value's text *is* its reason, which is the half of the
             * answer a developer reads. Asserting it means a refusal cannot
             * become a different refusal unnoticed. */
            CssSlice reason = CssComputedValueText(&value);

            CssSelectText(reason, out, capacity);
        } else if (CssComputedValueWrite(&value, out, capacity, &length, NULL) !=
                   RoboOk) {
            out[0] = '\0';
        }
    }
    CssComputedValueDispose(&value);
    CssDeclarationListDispose(&list);
}

/**
 * The matrix: every supported property, one declaration, one exact answer.
 *
 * The kind is asserted beside the text on purpose. `width: 50%` printing as `50%`
 * is unremarkable; being a PERCENTAGE rather than a LENGTH is the contract, and a
 * test that asserted only the text would pass for a build that had started
 * resolving percentages into pixels.
 */
static void TestCssConformanceMatrixDeclarations(void)
{
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);

    for (index = 0u; index < sizeof(cssConformanceDeclarations) /
                                sizeof(cssConformanceDeclarations[0]);
         index++) {
        const CssConformanceDeclaration *row = &cssConformanceDeclarations[index];
        char text[RoboCssComputedValueTextMax];
        const char *kind = NULL;

        CssConformanceShapeOf(registry, row, text, sizeof(text), &kind);
        RoboCheckStrEq(kind, row->kind);
        RoboCheckStrEq(text, row->text);
    }

    CssPropertyRegistryDestroy(registry);
}

/**
 * The matrix is a fact about the *product*, not about the normaliser.
 *
 * The table above drives `CssComputedValueCompute` directly. This one
 * puts the same declarations on real elements through the whole pipeline —
 * tokenizer, parser, the selector grammar, the cascade, inheritance — and asserts
 * the same answers arrive. That is what makes the matrix a statement about the
 * runtime rather than about one function, and it is the assertion that would fail
 * if the cascade ever handed the normaliser a different value than the one the
 * author wrote.
 *
 * The host holds at most `CSS_HOST_MAX_ELEMENTS` elements, so the rows are walked
 * in chunks: one sheet, one unique class per row, one element per row.
 */
static void TestCssConformanceMatrixThroughThePipeline(void)
{
    const size_t total = sizeof(cssConformanceDeclarations) /
                         sizeof(cssConformanceDeclarations[0]);
    size_t base;

    CssFixture();

    for (base = 0u; base < total; base += CSS_HOST_MAX_ELEMENTS) {
        CssInheritFixture fixture;
        char sheetText[4096];
        size_t offset = 0u;
        size_t count = total - base < CSS_HOST_MAX_ELEMENTS ? total - base
                                                            : CSS_HOST_MAX_ELEMENTS;
        size_t rowIndex;
        CssElementRef refs[CSS_HOST_MAX_ELEMENTS];
        /*
         * The class names live here rather than in the loop below, and that is
         * load-bearing: a host element *borrows* the bytes it is given, so a name
         * held in a per-iteration local is a name dangling by the time the sheet
         * is added — which fails as a handful of elements silently matching
         * nothing, not as a crash.
         */
        char classNames[CSS_HOST_MAX_ELEMENTS][16];

        CssInheritBegin(&fixture);

        /*
         * One rule per row, each naming the element that carries it. Composing the
         * sheet from the same table the expectations come from is deliberate: a
         * second hand-written sheet would be a second thing to keep in step, and
         * the point of the test is that *this* table survives the pipeline.
         */
        for (rowIndex = 0u; rowIndex < count; rowIndex++) {
            char selector[32];

            (void)snprintf(classNames[rowIndex], sizeof(classNames[rowIndex]), "c%u",
                           (unsigned)rowIndex);
            (void)snprintf(selector, sizeof(selector), ".%s", classNames[rowIndex]);
            refs[rowIndex] = CssHostAdd(CssInheritHost(&fixture), "div", NULL,
                                           classNames[rowIndex]);
            CssShapeText(sheetText, sizeof(sheetText), &offset, selector);
            CssShapeText(sheetText, sizeof(sheetText), &offset, " { ");
            CssShapeText(sheetText, sizeof(sheetText), &offset,
                           cssConformanceDeclarations[base + rowIndex].declaration);
            CssShapeText(sheetText, sizeof(sheetText), &offset, " }\n");
        }
        RoboCheckEqInt(offset < sizeof(sheetText), 1);
        CssHostSeal(CssInheritHost(&fixture));
        CssInheritSheet(&fixture, RoboCssOriginAuthor, sheetText);

        for (rowIndex = 0u; rowIndex < count; rowIndex++) {
            const CssConformanceDeclaration *row =
                &cssConformanceDeclarations[base + rowIndex];
            CssComputedStyle *style = NULL;
            CssError error = RoboCssCleared;

            RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
            /* A sheet of valid declarations produces no CSS *problem* even where
             * a declaration is refused: a refused value is an answer, not an
             * error, and the diagnostic rides on the computed value. */
            RoboCheckEqInt(CssStyleContextComputeStyle(
                                  CssInheritContext(&fixture), refs[rowIndex], style,
                                  &error, NULL),
                              RoboOk);
            RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
            /* The pipeline and the direct computation agree, text for text. */
            {
                char text[RoboCssComputedValueTextMax];

                CssComputedNote(style, row->property, text, sizeof(text));
                RoboCheckStrEq(text, row->text);
            }
            /* Every one of them was *declared* on its element: a value that
             * reached the style by inheritance or by default would mean the rule
             * never matched. Asked as the flag rather than as the rendered set,
             * because a refused declaration is `declared+invalid` and a reference
             * is `declared+unresolved` — both declared, and the flag is the half
             * this test is about. */
            RoboCheckEqInt(
                CssComputedStyleHasFlag(style, CssSliceFromText(
                                                            row->property),
                                                 RoboCssComputedFlagDeclared),
                1);
            /* And an INVALID declaration is counted as incomplete rather than
             * silently dropped, which is how a consumer sees how much of a style
             * it can trust. A clean declaration must *not* count, or the counter
             * would say nothing. */
            if (strcmp(row->kind, "invalid") == 0) {
                RoboCheck(CssComputedStyleIncompleteCount(style) > 0u);
                RoboCheckEqInt(CssComputedStyleHasInvalid(style), 1);
            }
            CssComputedStyleDestroy(style);
        }

        CssInheritEnd(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* The document                                                        */
/* ------------------------------------------------------------------ */

/*
 * One sheet, one tree, every family on the same element.
 *
 * The per-family pipeline tests each assert their family over the real DOM. This
 * is the one place they are asserted *together*, which is what makes it a
 * statement about the product: a `.card` is display, position, an inset, a
 * colour, four margins, four paddings, four border widths, a border style, a
 * border colour and a background, and a build that had any one of those families
 * disagreeing with the others would still pass twelve separate suites.
 */
#define CSS_CONFORMANCE_SHEET \
    "div.card {\n" \
    "    display: block;\n" \
    "    position: relative;\n" \
    "    top: 10px;\n" \
    "    width: 50%;\n" \
    "    height: 120px;\n" \
    "    margin: 10px 20px;\n" \
    "    padding: 5px;\n" \
    "    border: 1px solid black;\n" \
    "    background-color: #ffffff;\n" \
    "    color: #336699;\n" \
    "    z-index: 3;\n" \
    "}\n" \
    "span.label {\n" \
    "    border-top-style: dashed;\n" \
    "    margin-left: 4px;\n" \
    "}\n"

/**
 * The card: every family at once, on one element, plus the split on a child.
 *
 * The child is where the inheritance story is stated as a *product* fact rather
 * than as a per-property one: `color` is inherited, so the label is `#336699`; the
 * border and the background are not, so the label has no entry for them at all —
 * and it declares its own `border-top-style` and `margin-left`, which is what
 * shows that "no entry" is about the properties it did *not* declare.
 */
static void TestCssConformanceDocumentEveryFamily(void)
{
    CssInheritFixture fixture;
    CssElementRef card;
    CssElementRef label;
    CssComputedStyle *cardStyle = NULL;
    CssComputedStyle *labelStyle = NULL;
    CssError error = RoboCssCleared;

    CssFixture();
    CssInheritBegin(&fixture);
    card = CssHostAdd(CssInheritHost(&fixture), "div", NULL, "card");
    label = CssHostAdd(CssInheritHost(&fixture), "span", NULL, "label");
    CssHostSetParent(CssInheritHost(&fixture), label, card);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, CSS_CONFORMANCE_SHEET);
    RoboCheckEqInt(CssComputedStyleCreate(&cardStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&labelStyle, NULL), RoboOk);

    RoboCheckEqInt(CssStyleContextComputeStyle(
                          CssInheritContext(&fixture), card, cardStyle, &error, NULL),
                      RoboOk);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);

    /* Display and position: the two typed enumerator shapes. */
    CssComputedIs(cardStyle, "display", "block");
    CssComputedIs(cardStyle, "position", "relative");
    /* An inset and a dimenison, each keeping its own type. */
    CssComputedIs(cardStyle, "top", "10px");
    CssComputedIs(cardStyle, "width", "50%");
    CssComputedIs(cardStyle, "height", "120px");
    CssComputedIs(cardStyle, "z-index", "3");

    /* The `margin: 10px 20px` shorthand, distributed: vertical and horizontal. */
    CssComputedIs(cardStyle, "margin-top", "10px");
    CssComputedIs(cardStyle, "margin-right", "20px");
    CssComputedIs(cardStyle, "margin-bottom", "10px");
    CssComputedIs(cardStyle, "margin-left", "20px");
    /* `padding: 5px`, distributed evenly. */
    CssComputedIs(cardStyle, "padding-top", "5px");
    CssComputedIs(cardStyle, "padding-right", "5px");
    CssComputedIs(cardStyle, "padding-bottom", "5px");
    CssComputedIs(cardStyle, "padding-left", "5px");

    /* `border: 1px solid black`, distributed over three kinds and four sides. */
    CssComputedIs(cardStyle, "border-top-width", "1px");
    CssComputedIs(cardStyle, "border-right-width", "1px");
    CssComputedIs(cardStyle, "border-bottom-width", "1px");
    CssComputedIs(cardStyle, "border-left-width", "1px");
    CssComputedIs(cardStyle, "border-top-style", "solid");
    CssComputedIs(cardStyle, "border-right-style", "solid");
    CssComputedIs(cardStyle, "border-bottom-style", "solid");
    CssComputedIs(cardStyle, "border-left-style", "solid");
    CssComputedIs(cardStyle, "border-top-color", "#000000");
    CssComputedIs(cardStyle, "border-left-color", "#000000");

    /* The background, and the colour every `currentColor` would resolve against. */
    CssComputedIs(cardStyle, "background-color", "#ffffff");
    CssComputedIs(cardStyle, "color", "#336699");

    /* A declared value is `declared` and nothing else — not defaulted, not
     * inherited — for every family at once. */
    CssComputedFlagsAre(cardStyle, "display", "declared");
    CssComputedFlagsAre(cardStyle, "margin-left", "declared");
    CssComputedFlagsAre(cardStyle, "border-top-style", "declared");
    CssComputedFlagsAre(cardStyle, "background-color", "declared");
    /*
     * The split between the two kinds of property is the fact worth stating here,
     * and it is not the one a reader might expect:
     *
     *   a NON-inherited property nothing declared has *no entry at all* — which is
     *   what makes "this element has no border" a different statement from "this
     *   element has a border of zero width";
     *   an INHERITED property is always present, because it always has an answer:
     *   the parent's, or the registry's initial value with the `default` flag.
     *
     * `letter-spacing` therefore has an entry on an element that never mentions
     * it, and `border-top-left-radius` — a property this build does not have at
     * all — has none either way.
     */
    CssComputedIs(cardStyle, "border-top-left-radius", "<absent>");
    CssComputedIs(cardStyle, "letter-spacing", "normal");
    CssComputedFlagsAre(cardStyle, "letter-spacing", "default");

    /* --- the child ------------------------------------------------------- */

    RoboCheckEqInt(CssStyleContextComputeStyle(
                          CssInheritContext(&fixture), label, labelStyle, &error, NULL),
                      RoboOk);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);

    /* `color` is inherited, so the label has the card's *computed* colour. */
    CssComputedIs(labelStyle, "color", "#336699");
    CssComputedFlagsAre(labelStyle, "color", "inherited");
    /* What it declared itself won. */
    CssComputedIs(labelStyle, "border-top-style", "dashed");
    CssComputedFlagsAre(labelStyle, "border-top-style", "declared");
    CssComputedIs(labelStyle, "margin-left", "4px");
    /* A non-inherited family leaves no entry at all, rather than a default that
     * looked like an inherited zero. */
    CssComputedIs(labelStyle, "margin-top", "<absent>");
    CssComputedIs(labelStyle, "padding-top", "<absent>");
    CssComputedIs(labelStyle, "background-color", "<absent>");
    CssComputedIs(labelStyle, "border-left-style", "<absent>");
    /* And an inherited property *is* present on the child, sourced from the
     * parent — the other half of the same rule. */
    CssComputedIs(labelStyle, "letter-spacing", "normal");
    CssComputedFlagsAre(labelStyle, "letter-spacing", "inherited+default");
    /* The border colour is *not* inherited either — so `currentColor` on the
     * label is resolved against the label's own inherited `color` only when the
     * label actually declares a border colour. It does not, so there is no
     * entry, and the reference has nowhere to appear. */
    CssComputedIs(labelStyle, "border-left-color", "<absent>");

    /* Nothing in a clean document is incomplete or truncated. */
    RoboCheckEqInt((int)CssComputedStyleIncompleteCount(cardStyle), 0);
    RoboCheckEqInt((int)CssComputedStyleIncompleteCount(labelStyle), 0);
    RoboCheckEqInt(CssComputedStyleIsTruncated(cardStyle), 0);
    RoboCheckEqInt(CssComputedStyleIsTruncated(labelStyle), 0);

    CssComputedStyleDestroy(labelStyle);
    CssComputedStyleDestroy(cardStyle);
    CssInheritEnd(&fixture);
}

/**
 * The cascade's three keys, exercised across families in one document.
 *
 * Each element here is a different *key* winning: specificity, source order,
 * importance, and the inline attribute. They are one test rather than four
 * because the claim is that a single document can exercise all of them at once
 * and each property still lands on the right declaration — which is a statement
 * about the cascade being one machine rather than four.
 *
 * The inline seam is *per context* rather than per element: the stub answers the
 * same text for every element it is asked about. So the attribute here names
 * `margin-top` and not `color` — which is the assertion that matters anyway, since
 * it shows the inline style is a per-property *level* and not a replacement.
 */
static void TestCssConformanceDocumentCascadeAcrossFamilies(void)
{
    static const char *const sheet =
        "div { color: #111111; margin-top: 1px; border-top-style: solid }\n"
        "#by-id { color: #222222 }\n"
        "div.by-class { color: #333333 }\n"
        "div.by-class { color: #444444 }\n"
        "div.by-important { color: #555555 !important }\n"
        "div.by-important { color: #666666 }\n"
        "#later { color: #777777; border-top-style: dotted }\n"
        "div { border-top-style: dashed }\n";
    CssInheritFixture fixture;
    CssInlineStub inlineStub;
    CssElementRef ById;
    CssElementRef byClass;
    CssElementRef byImportant;
    CssElementRef bySourceOrder;
    CssElementRef byInline;

    CssFixture();
    CssInheritBegin(&fixture);
    /*
     * One inline value for the whole document, and it deliberately names a
     * property the sheet *also* declares — `margin-top` — so the contest below
     * is a real one between the inline level and the author's type rule. It names
     * nothing else, so every `color` assertion above it is about the author's own
     * rules and the attribute is not accidentally standing in for them.
     */
    CssInheritInline(&fixture, &inlineStub, "margin-top: 9px");

    ById = CssHostAdd(CssInheritHost(&fixture), "div", "by-id", NULL);
    byClass = CssHostAdd(CssInheritHost(&fixture), "div", NULL, "by-class");
    byImportant = CssHostAdd(CssInheritHost(&fixture), "div", NULL, "by-important");
    bySourceOrder = CssHostAdd(CssInheritHost(&fixture), "div", "later", NULL);
    byInline = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, sheet);

    /* Specificity: the id beats the class beats the type. */
    {
        CssComputedStyle *style = NULL;
        CssError error = RoboCssCleared;

        RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
        RoboCheckEqInt(CssStyleContextComputeStyle(
                              CssInheritContext(&fixture), ById, style, &error, NULL),
                          RoboOk);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        /* The id rule wins the colour, and the *type* rule still wins a property
         * the id rule never mentioned. (`margin-top` is deliberately not asserted
         * here: the inline attribute of this fixture names it, and the stub is
         * per context, so it would be `9px` on every element of the document. The
         * type rule's reach is shown by `border-top-style` instead.) */
        CssComputedIs(style, "color", "#222222");
        CssComputedIs(style, "border-top-style", "dashed");
        /* Three rules cover `border-top-style` and only the last `div` one
         * matches this element, so `dashed`. `#later`'s `dotted` belongs to the
         * element whose id *is* `later`, which is asserted just below — and that
         * pairing is the point: an id rule first in the sheet still beats a type
         * rule after it. */
        CssComputedIs(style, "border-top-style", "dashed");
        CssComputedStyleDestroy(style);
    }
    /* Specificity beats source order, and the losing rule is later in the sheet. */
    {
        CssComputedStyle *style = NULL;

        RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
        RoboCheckEqInt(CssStyleContextComputeStyle(
                              CssInheritContext(&fixture), bySourceOrder, style, NULL,
                              NULL),
                          RoboOk);
        CssComputedIs(style, "color", "#777777");
        CssComputedIs(style, "border-top-style", "dotted");
        CssComputedStyleDestroy(style);
    }
    /* Source order among equals: the second `.by-class` rule wins the colour. */
    {
        CssComputedStyle *style = NULL;

        RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
        RoboCheckEqInt(CssStyleContextComputeStyle(
                              CssInheritContext(&fixture), byClass, style, NULL, NULL),
                          RoboOk);
        CssComputedIs(style, "color", "#444444");
        CssComputedStyleDestroy(style);
    }
    /* Importance: the `!important` declaration outranks the later one. */
    {
        CssComputedStyle *style = NULL;

        RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
        RoboCheckEqInt(CssStyleContextComputeStyle(
                              CssInheritContext(&fixture), byImportant, style, NULL, NULL),
                          RoboOk);
        CssComputedIs(style, "color", "#555555");
        CssComputedStyleDestroy(style);
    }
    /*
     * The inline level, and the two halves of what a level *is*: the attribute
     * beats the author's rule on the property it names, and the author's rule
     * still applies to every property it does not. A cascade that let the
     * attribute replace the style would fail the second half, and one that
     * ignored it would fail the first.
     */
    {
        CssComputedStyle *style = NULL;

        RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
        RoboCheckEqInt(CssStyleContextComputeStyle(
                              CssInheritContext(&fixture), byInline, style, NULL, NULL),
                          RoboOk);
        CssComputedIs(style, "margin-top", "9px");
        CssComputedFlagsAre(style, "margin-top", "declared");
        /* Not named by the attribute, so the type rule's answer stands. */
        CssComputedIs(style, "color", "#111111");
        CssComputedIs(style, "border-top-style", "dashed");
        CssComputedStyleDestroy(style);
    }
    /* The stub was asked, once per element the context computed. */
    RoboCheckEqInt(inlineStub.calls >= 5, 1);

    CssInheritEnd(&fixture);
}

/**
 * Both orders of shorthand and longhand, across every family that distributes.
 *
 * `margin: 10px; margin-left: 20px` and `margin-left: 20px; margin: 10px` are the
 * same two declarations in the other order, and the second one's shorthand wins
 * the left edge. That is CSS, it is the one rule a "populate what you can"
 * implementation gets wrong while every value it writes is individually correct,
 * and it is asserted here for all five distributing families at once so that a
 * family whose pass ran in a different order would show up.
 */
static void TestCssConformanceDocumentShorthandAndLonghand(void)
{
    static const struct {
        const char *label;        /* the property the answer is read from */
        const char *longhandLast;
        const char *shorthandLast;
        const char *longhandValue;   /* what the longhand's own declaration says */
        const char *shorthandValue;  /* what the shorthand gives that property */
    } cases[] = {
        {"margin-left", "margin: 10px; margin-left: 20px",
         "margin-left: 20px; margin: 10px", "20px", "10px"},
        {"padding-left", "padding: 10px; padding-left: 20px",
         "padding-left: 20px; padding: 10px", "20px", "10px"},
        {"border-left-width", "border-width: 1px; border-left-width: 3px",
         "border-left-width: 3px; border-width: 1px", "3px", "1px"},
        {"border-left-style", "border-style: solid; border-left-style: dashed",
         "border-left-style: dashed; border-style: solid", "dashed", "solid"},
        {"border-left-color", "border-color: #111111; border-left-color: #222222",
         "border-left-color: #222222; border-color: #111111", "#222222", "#111111"},
        /* The composite shorthands beside the positional ones. */
        {"border-top-width", "border-width: 1px; border-top: 3px solid",
         "border-top: 3px solid; border-width: 1px", "3px", "1px"},
        {"border-top-style", "border-style: solid; border-top: 2px dashed",
         "border-top: 2px dashed; border-style: solid", "dashed", "solid"},
        {"border-top-color", "border-color: #111111; border-top: 2px solid #222222",
         "border-top: 2px solid #222222; border-color: #111111", "#222222", "#111111"}
    };
    size_t index;

    CssFixture();

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        CssInheritFixture fixture;
        CssElementRef longhandLast;
        CssElementRef shorthandLast;
        CssComputedStyle *style = NULL;

        CssInheritBegin(&fixture);
        longhandLast = CssHostAdd(CssInheritHost(&fixture), "div", NULL, "a");
        shorthandLast = CssHostAdd(CssInheritHost(&fixture), "div", NULL, "b");
        CssHostSeal(CssInheritHost(&fixture));
        {
            char sheetText[512];
            size_t offset = 0u;

            CssShapeText(sheetText, sizeof(sheetText), &offset, "div.a { ");
            CssShapeText(sheetText, sizeof(sheetText), &offset, cases[index].longhandLast);
            CssShapeText(sheetText, sizeof(sheetText), &offset, " }\n");
            CssShapeText(sheetText, sizeof(sheetText), &offset, "div.b { ");
            CssShapeText(sheetText, sizeof(sheetText), &offset,
                           cases[index].shorthandLast);
            CssShapeText(sheetText, sizeof(sheetText), &offset, " }\n");
            CssInheritSheet(&fixture, RoboCssOriginAuthor, sheetText);
        }

        /* Longhand written last: it wins. */
        RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
        RoboCheckEqInt(CssStyleContextComputeStyle(
                              CssInheritContext(&fixture), longhandLast, style, NULL, NULL),
                          RoboOk);
        CssComputedIs(style, cases[index].label, cases[index].longhandValue);
        CssComputedStyleDestroy(style);

        /* Shorthand written last: it wins, and that is the whole rule. */
        style = NULL;
        RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
        RoboCheckEqInt(CssStyleContextComputeStyle(
                              CssInheritContext(&fixture), shorthandLast, style, NULL,
                              NULL),
                          RoboOk);
        CssComputedIs(style, cases[index].label, cases[index].shorthandValue);
        CssComputedStyleDestroy(style);

        CssInheritEnd(&fixture);
    }
}

/**
 * The `border` shorthand against the two things that also cover its longhands.
 *
 * `border: 1px solid red` stands for twelve longhands; `border-top: 2px dashed`
 * stands for three of them; `border-top-width: 4px` for one. All three are on the
 * *same* element, at the *same* specificity, so the winner is source order — and
 * the four other sides must still be exactly what `border` gave them. That is the
 * ownership question stated as a product fact: if two passes materialised these
 * longhands, one side would silently disagree.
 */
static void TestCssConformanceDocumentThreeDeclarers(void)
{
    CssInheritFixture fixture;
    CssElementRef div;
    CssComputedStyle *style = NULL;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div {\n"
                      "    border: 1px solid #111111;\n"
                      "    border-top: 2px dashed;\n"
                      "    border-top-width: 4px;\n"
                      "}\n");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssStyleContextComputeStyle(
                          CssInheritContext(&fixture), div, style, NULL, NULL),
                      RoboOk);

    /* The top edge is what the three declarations together say: the width from
     * the longhand, the style from the side shorthand, the colour from `border`
     * — which is because `border-top: 2px dashed` *reset* the colour it did not
     * name, and then `border` no longer wins that longhand. */
    CssComputedIs(style, "border-top-width", "4px");
    CssComputedIs(style, "border-top-style", "dashed");
    /* The other three sides are untouched by either later declaration. */
    CssComputedIs(style, "border-right-width", "1px");
    CssComputedIs(style, "border-bottom-width", "1px");
    CssComputedIs(style, "border-left-width", "1px");
    CssComputedIs(style, "border-right-style", "solid");
    CssComputedIs(style, "border-bottom-style", "solid");
    CssComputedIs(style, "border-left-style", "solid");
    CssComputedIs(style, "border-right-color", "#111111");
    CssComputedIs(style, "border-bottom-color", "#111111");
    CssComputedIs(style, "border-left-color", "#111111");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pipeline holds                                                  */
/* ------------------------------------------------------------------ */

/*
 * Malformed and hostile documents, driven through the *whole* chain.
 *
 * `css.tokenize.malformed_corpus` drives the tokenizer and `css.robust.*` drives
 * the models. Neither reaches the pipeline, and the pipeline is where a document
 * can be wrong in ways a token never is: a rule whose prelude is not a selector,
 * a declaration whose value is empty, three hundred unclosed blocks, a custom
 * property that references itself. The claim is not that they parse — it is that
 * every door which promises RoboOk keeps that promise, the style still exists,
 * and the allocator comes back to where it started.
 */
static const char *const cssConformanceMalformed[] = {
    "",
    "   \n\t ",
    "/* unterminated",
    "div {",
    "div }",
    "div { color: }",
    "div { : red }",
    "div { color red }",
    "div { color: red",
    "div { color: red; margin: }",
    "div { color: rgb( }",
    "div { color: rgb(1, 2) }",
    "div { color: #12 }",
    "div { padding: auto }",
    "div { display: }",
    "div { display: 20px }",
    "div { position: banana }",
    "div { z-index: 1.5 }",
    "div { border-width: 1px 2px 3px 4px 5px }",
    "div { background: url( }",
    "div { background-position: left top bottom }",
    "div { width: calc(100% - ) }",
    "div { width: var(--x) }",
    "div { --a: var(--b); --b: var(--a); color: var(--a) }",
    "div { color: var(--missing) }",
    "div { color: var(--) }",
    "div { color: inherit inherit }",
    "div { color: revert }",
    "div { color: !important }",
    "div { color: red !important }",
    "div { color: red !important !important }",
    "div { color: ; color: red }",
    "@media screen { div { color: red } }",
    "@import url(nope.css);",
    "@font-face { }",
    "div { color: \xC3\x28 }",
    /* Two bytes of a three-byte sequence, and a control byte used as a property
     * name's first character: neither is a token sequence, and both are input an
     * attacker controls. */
    "div { \x01: red }",
    "div { color: \"unterminated }",
    "div div div div div div div div { color: red }",
    "div::before { content: none }",
    "div:not(:not(:not(div))) { color: red }",
    "div { color: red } } } }"
};

/**
 * Every malformed document survives the whole pipeline.
 *
 * Each is parsed, added as a sheet, and used to compute one element's style. The
 * assertions are the four a pipeline owes: the doors that promise success succeed,
 * the style exists and is usable, the counters are self-consistent, and the
 * process is still alive at the end to say so. The last one is not a joke — it is
 * the entire reason a crash-safety corpus exists, and it is the assertion whose
 * failure mode is a dead test binary.
 */
static void TestCssConformancePipelineMalformedCorpus(void)
{
    size_t index;

    CssFixture();

    for (index = 0u; index < sizeof(cssConformanceMalformed) /
                                sizeof(cssConformanceMalformed[0]);
         index++) {
        CssInheritFixture fixture;
        CssElementRef div;
        CssComputedStyle *style = NULL;
        CssError error = RoboCssCleared;
        int enabled = CssAllocatorEnsureEnabled();
        size_t baseline = MemDebugLiveCount();

        CssInheritBegin(&fixture);
        div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
        CssHostSeal(CssInheritHost(&fixture));
        /* A sheet that cannot parse is still a sheet: the parse reports and
         * returns RoboOk, and the rule list holds what survived. */
        CssInheritSheet(&fixture, RoboCssOriginAuthor,
                          cssConformanceMalformed[index]);

        RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
        /* The one door that must still work on any document. */
        RoboCheckEqInt(CssStyleContextComputeStyle(
                              CssInheritContext(&fixture), div, style, &error, NULL),
                          RoboOk);
        /* A style an author can read something out of, whatever the input was. */
        RoboCheck(style != NULL);
        if (style) {
            size_t incomplete = CssComputedStyleIncompleteCount(style);

            /* The counter is a count of entries, so it can never exceed them,
             * and `has_invalid` agrees with it: the two are derived from the same
             * flags and a disagreement would mean one of them was lying. */
            RoboCheck(incomplete <= CssComputedStyleCount(style));
            if (CssComputedStyleHasInvalid(style)) {
                RoboCheck(incomplete > 0u);
            }
            /*
             * The query door still answers, on a document that may have parsed to
             * nothing at all — and the answer is the invariant, not its
             * provenance. `color` is inherited, so on a document with no readable
             * declaration it arrives from the registry's default (`fromInitial`
             * 0, because an inherited property always *has* an entry), while on
             * one whose declaration survived it is that declaration's answer. Both
             * are correct; what must never happen is a failure code or an empty
             * value.
             */
            {
                CssComputedValue value = RoboCssCleared;
                int fromInitial = -1;
                Result rc = CssStyleContextQueryComputedValue(
                    CssInheritContext(&fixture), style, CssSliceFromText("color"),
                    &value, &fromInitial, NULL);

                RoboCheckEqInt(rc, RoboOk);
                RoboCheckEqInt(fromInitial == 0 || fromInitial == 1, 1);
                RoboCheckEqInt(value.kind != RoboCssComputedValueNone, 1);
                /* An answer that came from the registry's initial is a colour,
                 * because `color`'s initial value is one. */
                if (fromInitial == 1) {
                    RoboCheckEqInt(value.kind, RoboCssComputedValueColor);
                }
                CssComputedValueDispose(&value);
            }
            CssComputedStyleDestroy(style);
        }
        CssInheritEnd(&fixture);

        /* Nothing leaked, for every one of the forty-two inputs. A malformed
         * document that leaked would be a malformed document that eventually
         * exhausted memory. */
        RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        CssAllocatorRestore(enabled);
    }
}

/**
 * Identical bytes, identical styles — asserted over the *whole* style.
 *
 * Per-family suites assert determinism of one value. This asserts it of the
 * *style*: two independent contexts, the same sheet and the same tree, and every
 * entry of the second equal to the corresponding entry of the first — by
 * `CssComputedValueEquals` rather than by text, so a difference in
 * formatting could not hide a difference in value and vice versa.
 */
static void TestCssConformancePipelineDeterminism(void)
{
    CssInheritFixture first;
    CssInheritFixture second;
    CssElementRef firstDiv;
    CssElementRef secondDiv;
    CssComputedStyle *firstStyle = NULL;
    CssComputedStyle *secondStyle = NULL;
    size_t index;

    CssFixture();
    CssInheritBegin(&first);
    CssInheritBegin(&second);
    firstDiv = CssHostAdd(CssInheritHost(&first), "div", NULL, "card");
    secondDiv = CssHostAdd(CssInheritHost(&second), "div", NULL, "card");
    CssHostSeal(CssInheritHost(&first));
    CssHostSeal(CssInheritHost(&second));
    CssInheritSheet(&first, RoboCssOriginAuthor, CSS_CONFORMANCE_SHEET);
    CssInheritSheet(&second, RoboCssOriginAuthor, CSS_CONFORMANCE_SHEET);
    RoboCheckEqInt(CssComputedStyleCreate(&firstStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&secondStyle, NULL), RoboOk);

    RoboCheckEqInt(CssStyleContextComputeStyle(
                          CssInheritContext(&first), firstDiv, firstStyle, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextComputeStyle(
                          CssInheritContext(&second), secondDiv, secondStyle, NULL, NULL),
                      RoboOk);

    /* The same number of entries, in the same order, under the same names. */
    RoboCheckEqInt((int)CssComputedStyleCount(firstStyle),
                      (int)CssComputedStyleCount(secondStyle));
    for (index = 0u; index < CssComputedStyleCount(firstStyle); index++) {
        const CssComputedValue *a =
            CssComputedStyleComputedAt(firstStyle, index);
        const CssComputedValue *b =
            CssComputedStyleComputedAt(secondStyle, index);
        CssSlice name = CssComputedStyleNameAt(firstStyle, index);
        CssSlice other = CssComputedStyleNameAt(secondStyle, index);
        const CssValue *specifiedA;
        const CssValue *specifiedB;

        /* The same *name* at the same index, so the comparison below is about
         * one property and not about two lists that happened to line up. */
        RoboCheckEqInt(name.length, other.length);
        RoboCheckEqInt(name.length > 0u, 1);
        specifiedA = CssComputedStyleSpecifiedByName(firstStyle, name);
        RoboCheck(specifiedA != NULL);
        /* The value is equal by structure, not by text: a difference in
         * formatting could not hide a difference in value, or the reverse. */
        RoboCheckEqInt(CssComputedValueEquals(a, b), 1);
        /* And the specified half agrees too, so a difference that only the
         * normaliser papered over would still show. */
        specifiedB = CssComputedStyleSpecifiedByName(secondStyle, other);
        RoboCheckEqInt(specifiedB != NULL, 1);
    }
    RoboCheck(CssComputedStyleCount(firstStyle) > 0u);

    CssComputedStyleDestroy(secondStyle);
    CssComputedStyleDestroy(firstStyle);
    CssInheritEnd(&second);
    CssInheritEnd(&first);
}

/**
 * One document, every stage, and each stage's product asserted.
 *
 * The conformance claim this phase makes is that the stages *compose*: the
 * tokenizer's tokens are the parser's value tokens, the parser's rules are what
 * the selector grammar reads, the selector grammar's answer is what the cascade
 * orders, and the cascade's winner is what the normaliser computes. Testing the
 * stages separately — which is what the other forty suites do, thoroughly — is
 * exactly how a mismatch *between* two of them goes unnoticed, because each stage
 * is only ever asked with input the test wrote.
 */
static void TestCssConformancePipelineTokenizerToComputed(void)
{
    static const char *const text =
        "div.card {\n"
        "    color: #336699;   /* the identifier and the hash */\n"
        "    margin: 10px 20px;\n"
        "}\n";
    CssInheritFixture fixture;
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    CssStylesheetId sheet;
    CssElementRef div;
    CssComputedStyle *style = NULL;

    CssFixture();

    /* --- 1. the tokenizer, over the very bytes the sheet will be given --- */
    CssSourceOfString(&source, text);
    (void)CssLex(&source, &log);
    RoboCheckEqInt(log.eofSeen, 1);
    RoboCheckEqInt(log.hasError, 0);
    /* The invariant the whole chain rests on: the tokens tile the source. */
    RoboCheckEqInt(CssLogTiles(&log, CssSourceLength(&source)), 1);
    /* A comment is one token and its bytes are the source's, so the parser below
     * sees the same stream this test just read. */
    RoboCheck(CssCountType(&log, RoboCssTokenComment) == 1u);
    RoboCheck(CssCountType(&log, RoboCssTokenHash) == 1u);
    RoboCheck(CssCountType(&log, RoboCssTokenIdent) > 0u);
    RoboCheck(CssCountType(&log, RoboCssTokenDimension) == 2u);

    /* --- 2. the parser, and the same tokens on the other side --- */
    sheet = CssParseSheet(text, NULL);
    RoboCheckEqInt((int)CssStylesheetRuleCount(sheet), 1);
    {
        const CssStyleRule *rule = CssSheetStyleRule(sheet, 0u);

        RoboCheck(rule != NULL);
        /* Two declarations, and the comment between them did not become a third
         * — nor did it attach itself to either value. */
        RoboCheckEqInt((int)CssStyleRuleDeclarationCount(rule), 2);
        RoboCheckEqInt(CssParseDeclarationIs(rule, 0u, "color"), 1);
        RoboCheckEqInt(CssParseDeclarationIs(rule, 1u, "margin"), 1);
        /* The value the grammar built is the *tokenizer's* tokens: the hash the
         * log found is the hash this value holds, spelled the same way. */
        {
            char shape[128];

            CssDeclarationShape(CssStyleRuleDeclarations(rule), 0u, shape,
                                  sizeof(shape));
            RoboCheckStrEq(shape, "#336699");
        }
    }
    /*
     * The prelude is *component values* and not a selector: `div.card` is an
     * ident, a delim and an ident, and the shape writes exactly that. This is the
     * boundary 9.3 draws and 9.4 crosses — the grammar stores what the tokens
     * were and the selector parser is what gives them meaning.
     */
    {
        char prelude[64];

        CssPreludeShape(CssSheetStyleRule(sheet, 0u), prelude, sizeof(prelude));
        RoboCheckStrEq(prelude, "div,.,card");
    }

    /* --- 3. selectors, and the specificity the cascade will order by --- */
    {
        CssSelectorList list = RoboCssCleared;
        CssSpecificity specificity;

        CssSelectParse("div.card", &list);
        RoboCheckEqInt((int)CssSelectorListCount(&list), 1);
        specificity = CssSpecificityOfListItem(&list, 0u);
        /* One class and one type: (0, 1, 1). */
        RoboCheckEqInt(specificity.id, 0u);
        RoboCheckEqInt(specificity.classLike, 1u);
        RoboCheckEqInt(specificity.typeLike, 1u);
        CssSelectorListDispose(&list);
    }

    /* --- 4. the cascade, over the sheet the parser produced --- */
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, "card");
    CssHostSeal(CssInheritHost(&fixture));
    /* The same sheet id the parse above returned, so this is the *parsed* rules
     * travelling rather than a re-parse of the same text. */
    RoboCheckEqInt(CssStylesheetSetOrigin(sheet, RoboCssOriginAuthor, NULL),
                      RoboOk);
    {
        size_t ruleIndex;

        for (ruleIndex = 0u; ruleIndex < CssStylesheetRuleCount(sheet);
             ruleIndex++) {
            CssStyleRule *rule = (CssStyleRule *)CssRuleStyle(
                CssStylesheetRule(sheet, ruleIndex));
            CssError ruleError = RoboCssCleared;

            if (rule) {
                RoboCheckEqInt(CssStyleRuleParseSelectors(
                                      rule, CssStylesheetSource(sheet), &ruleError,
                                      NULL),
                                  RoboOk);
            }
        }
    }
    RoboCheckEqInt(CssStyleContextAddSheet(fixture.cascade.context,
                                                      RoboCssOriginAuthor, sheet, NULL),
                      RoboOk);
    {
        CssCascade cascade = RoboCssCleared;
        char winner[64];

        RoboCheckEqInt(CssStyleContextMatchDeclarations(
                              fixture.cascade.context, div, &cascade, NULL, NULL),
                          RoboOk);
        /* The cascade found the rule, and the selector it matched is the class
         * one — so the specificity read above is the specificity it used. */
        RoboCheck(CssCascadeCount(&cascade) > 0u);
        RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", winner,
                                                  sizeof(winner)),
                          RoboCssCascadeVerdictDecided);
        RoboCheckStrEq(winner, "#336699");
        CssCascadeDispose(&cascade);
    }

    /* --- 5. inheritance, and the computed style it produces --- */
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssStyleContextComputeStyle(
                          fixture.cascade.context, div, style, NULL, NULL),
                      RoboOk);
    /* Both declaration kinds arrive typed, and the shorthand was distributed by
     * the same pass every other test in the build goes through. */
    CssComputedIs(style, "color", "#336699");
    CssComputedIs(style, "margin-top", "10px");
    CssComputedIs(style, "margin-right", "20px");
    CssComputedFlagsAre(style, "color", "declared");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
    /* The sheet this test created belongs to the fixture-free part above, so it
     * is released here rather than left in the module's fixed table. */
    (void)CssStylesheetDestroy(sheet, NULL);
    CssSourceDispose(&source);
}

/**
 * The whole pipeline returns the allocator to where it started.
 *
 * Every stage in this chain owns something — a source copies bytes, a sheet owns
 * rules, a cascade owns candidates, a resolved style owns value trees, a computed
 * style owns values and their text. A build that leaked at any one of them would
 * still pass every value assertion in the file.
 */
static void TestCssConformancePipelineLifetime(void)
{
    CssInheritFixture fixture;
    int enabled = CssAllocatorEnsureEnabled();
    size_t baseline;
    size_t round;

    CssFixture();
    baseline = MemDebugLiveCount();

    for (round = 0u; round < 8u; round++) {
        CssElementRef div;
        CssComputedStyle *style = NULL;

        CssInheritBegin(&fixture);
        div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, "card");
        CssHostSeal(CssInheritHost(&fixture));
        CssInheritSheet(&fixture, RoboCssOriginAuthor, CSS_CONFORMANCE_SHEET);
        RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
        RoboCheckEqInt(CssStyleContextComputeStyle(
                              CssInheritContext(&fixture), div, style, NULL, NULL),
                          RoboOk);
        CssComputedIs(style, "color", "#336699");
        /* The query door allocates its answer too, and it has to be released by
         * the caller: a value nothing disposes is the leak this catches. Asked
         * for a property this element genuinely has no entry for — non-inherited,
         * and not one the sheet names — so the answer really is the registry's
         * initial value rather than an inherited one. */
        {
            CssComputedValue value = RoboCssCleared;
            int fromInitial = -1;

            RoboCheckEqInt(CssStyleContextQueryComputedValue(
                                  CssInheritContext(&fixture), style,
                                  CssSliceFromText("background-image"), &value,
                                  &fromInitial, NULL),
                              RoboOk);
            RoboCheckEqInt(fromInitial, 1);
            RoboCheckEqInt(value.kind, RoboCssComputedValueKeyword);
            CssComputedValueDispose(&value);
        }
        CssComputedStyleDestroy(style);
        CssInheritEnd(&fixture);

        /* After every round, not only at the end: a leak that happened only on
         * the first iteration would be invisible in a single end-of-loop check. */
        RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    }

    /* And the same document parsed fresh each time is the same document. */
    CssAllocatorRestore(enabled);
}

/* ------------------------------------------------------------------ */
/* The build's promises                                                */
/* ------------------------------------------------------------------ */

/**
 * Every flag in the capability struct, asserted by name.
 *
 * This is the phase closing the one hole its own documentation left. 9.14 and
 * 9.15 each recorded that `CssGetCapabilities` reports `backgrounds` and
 * `borders` as 1, and **neither flag was ever asserted anywhere in the build** —
 * `css.capabilities` stops at `positioning` and then jumps to the two flags that
 * are 0. A documented promise with no test behind it is a promise that can stop
 * being true silently, which is exactly what a conformance phase exists to find.
 *
 * The list is written out rather than derived from the struct, so a field added
 * later is a deliberate act here and a *missing* field shows as a flag nothing
 * asserts. The count is pinned against the struct's own size for the same reason.
 */
static void TestCssConformanceCapabilitiesEveryFlag(void)
{
    static const struct {
        const char *name;
        int expected;
    } flags[] = {
        {"source_model", 1},        {"token_model", 1},
        {"value_model", 1},         {"property_registry", 1},
        {"declaration_model", 1},   {"selector_model", 1},
        {"rule_model", 1},          {"stylesheet_model", 1},
        {"error_model", 1},         {"tokenizer", 1},
        {"parser", 1},              {"selector_parsing", 1},
        {"selector_matching", 1},
        {"cascade", 1},             {"specificity", 1},
        {"inheritance", 1},         {"computed_values", 1},
        {"colors", 1},              {"units", 1},
        {"box_model", 1},           {"display", 1},
        {"positioning", 1},         {"backgrounds", 1},
        {"borders", 1},             {"overflow", 1},
        {"inline_styles", 1},       {"default_styles", 0},
        {"stylesheet_loading", 0},
        {"custom_properties", 1},   {"variable_substitution", 1},
        {"cssom", 1},               {"flexbox", 1},
        {"grid", 1},                {"tables", 1},
        {"multi_column", 1},
        {"writing_modes", 1},
        {"transforms", 1},
        {"transitions", 1},
        {"animations", 1},
        {"media_queries", 1},
        {"container_queries", 1},
        {"logical_properties", 1},
        {"fonts", 1},
        {"text", 1},
        {"images", 1},
        {"blendModes", 1},
        {"borderDecorations", 1},
        {"filters", 1}
    };
    CssCapabilities caps;
    int *asInts = (int *)&caps;
    size_t index;

    RoboCheckEqInt((int)(sizeof(flags) / sizeof(flags[0])),
                      (int)(sizeof(CssCapabilities) / sizeof(int)));

    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    for (index = 0u; index < sizeof(flags) / sizeof(flags[0]); index++) {
        /*
         * The flags are `int` fields in declaration order with no padding, which
         * the size assertion above pins; reading them positionally is what lets
         * one list cover the struct without a second copy of every member's name.
         */
        if (asInts[index] != flags[index].expected) {
            TestFail(__FILE__, __LINE__, "caps.%s == %d (got %d)", flags[index].name,
                           flags[index].expected, asInts[index]);
        }
    }

    /* The two that were never asserted, asserted the other way round as well: a
     * flag that is 1 is a promise the *suite* keeps, so the families this phase
     * exercises and the families the build claims are the same set. */
    RoboCheckEqInt(caps.backgrounds, 1);
    RoboCheckEqInt(caps.borders, 1);
    /* The same habit for 18.14, whose flag is asserted here *and* by its own suite:
     * two questions, because a flag is a promise about the build and a family is a
     * promise about a module. */
    RoboCheckEqInt(caps.fonts, 1);
    /* And for 18.15, the same pair of promises. */
    RoboCheckEqInt(caps.text, 1);
    /* And for 18.16. */
    RoboCheckEqInt(caps.images, 1);
    /* And for 18.17, whose flag the background suite asserts too. */
    RoboCheckEqInt(caps.blendModes, 1);
    /* And for 18.18. */
    RoboCheckEqInt(caps.borderDecorations, 1);
    /* And for 18.19. */
    RoboCheckEqInt(caps.filters, 1);

    /* The frozen-surface flag is a promise about the phase, not a capability. */
    RoboCheckEqInt(RoboCssApiFrozen, 0);
    RoboCheckEqInt(RoboCssVersionMajor, 0);
    RoboCheckEqInt(RoboCssVersionMinor, 37);
}

void TestRegisterCssConformance(void);

void TestRegisterCssConformance(void)
{
    TestRegister("css.conformance.registry.every_initial_computes",
                       TestCssConformanceRegistryEveryInitialComputes);
    TestRegister("css.conformance.registry.reference_initial",
                       TestCssConformanceRegistryReferenceInitial);
    TestRegister("css.conformance.registry.computations_are_known",
                       TestCssConformanceRegistryComputationsAreKnown);
    TestRegister("css.conformance.matrix.declarations",
                       TestCssConformanceMatrixDeclarations);
    TestRegister("css.conformance.matrix.through_the_pipeline",
                       TestCssConformanceMatrixThroughThePipeline);
    TestRegister("css.conformance.document.every_family",
                       TestCssConformanceDocumentEveryFamily);
    TestRegister("css.conformance.document.cascade_across_families",
                       TestCssConformanceDocumentCascadeAcrossFamilies);
    TestRegister("css.conformance.document.shorthand_and_longhand",
                       TestCssConformanceDocumentShorthandAndLonghand);
    TestRegister("css.conformance.document.three_declarers",
                       TestCssConformanceDocumentThreeDeclarers);
    TestRegister("css.conformance.pipeline.tokenizer_to_computed",
                       TestCssConformancePipelineTokenizerToComputed);
    TestRegister("css.conformance.pipeline.malformed_corpus",
                       TestCssConformancePipelineMalformedCorpus);
    TestRegister("css.conformance.pipeline.determinism",
                       TestCssConformancePipelineDeterminism);
    TestRegister("css.conformance.pipeline.lifetime",
                       TestCssConformancePipelineLifetime);
    TestRegister("css.conformance.capabilities.every_flag",
                       TestCssConformanceCapabilitiesEveryFlag);
}
