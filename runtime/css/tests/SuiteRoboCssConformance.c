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
typedef struct css_conformance_initial {
    const char *name;
    const char *kind;
    const char *text;
} css_conformance_initial_t;

static const css_conformance_initial_t css_conformance_initials[] = {
    /* The nine common inherited properties and their font/text relatives. */
    {"color", "colour", "#000000"},
    {"cursor", "keyword", "auto"},
    {"direction", "keyword", "ltr"},
    {"font-family", "family-list", "serif"},
    /* `medium` is a keyword and not a length: `font-size: medium` is the user
     * agent's default size, which is a different statement from a pixel value. */
    {"font-size", "keyword", "medium"},
    {"font-style", "keyword", "normal"},
    {"font-variant", "keyword", "normal"},
    {"font-weight", "keyword", "normal"},
    {"letter-spacing", "keyword", "normal"},
    {"line-height", "keyword", "normal"},
    {"text-align", "keyword", "start"},
    /* A bare `0` is CSS's one number that *is* a length, and it arrives as `0px`. */
    {"text-indent", "length", "0px"},
    {"text-transform", "keyword", "none"},
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
    /* Positioning: an inset starts at `auto` and `z-index` at `auto`, neither of
     * which is zero — the one pair of initial values whose wrong answer would
     * look entirely plausible. */
    {"bottom", "keyword", "auto"},
    {"box-sizing", "keyword", "content-box"},
    {"display", "display", "inline"},
    {"height", "keyword", "auto"},
    {"left", "keyword", "auto"},
    /* The box model's eight edges: `margin`/`padding` are shorthands (PRESERVED)
     * and their eight longhands are typed. */
    {"margin", "preserved", "0"},
    {"margin-bottom", "length", "0px"},
    {"margin-left", "length", "0px"},
    {"margin-right", "length", "0px"},
    {"margin-top", "length", "0px"},
    {"padding", "preserved", "0"},
    {"padding-bottom", "length", "0px"},
    {"padding-left", "length", "0px"},
    {"padding-right", "length", "0px"},
    {"padding-top", "length", "0px"},
    {"position", "position", "static"},
    {"right", "keyword", "auto"},
    {"top", "keyword", "auto"},
    {"width", "keyword", "auto"},
    {"z-index", "keyword", "auto"}
};

/**
 * Every registered property's initial value computes to a valid, typed value.
 *
 * This is the invariant that spans the whole table, and the one a per-family
 * suite cannot state: it is not about a family, it is about the *registry* and
 * the normaliser agreeing. A row whose `computation` does not describe its
 * `initial_text` — a `display` shape on `border-width`, a `colour` shape on
 * `padding` — computes INVALID here and nowhere else.
 *
 * The kind, the canonical text and the row's own identity are asserted together,
 * because a test that checked only "not INVALID" would pass for a property that
 * had quietly been given another property's shape.
 */
static void test_css_conformance_registry_every_initial_computes(void)
{
    robo_css_property_registry_t *registry = NULL;
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);
    ROBO_CHECK(registry != NULL);

    /*
     * The table is the coverage, so it is asserted to be the whole registry before
     * it is walked. A property added to the registry without a row here fails
     * *this* line rather than silently going untested.
     */
    ROBO_CHECK_EQ_INT((int)(sizeof(css_conformance_initials) /
                            sizeof(css_conformance_initials[0])),
                      (int)robo_css_property_registry_count(registry));

    for (index = 0u; index < robo_css_property_registry_count(registry); index++) {
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_at(registry, index);
        const robo_css_value_t *initial;
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;
        char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
        size_t length = 0u;

        ROBO_CHECK(definition != NULL);
        if (!definition) {
            continue;
        }
        /*
         * The table is in the registry's order, so a row that had drifted is
         * caught by name rather than by a count that happened to still agree.
         */
        if (index < sizeof(css_conformance_initials) / sizeof(css_conformance_initials[0])) {
            ROBO_CHECK_STR_EQ(definition->name, css_conformance_initials[index].name);
        }
        /* Every row declares a computation, and it is one this build knows. */
        ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known(definition->computation), 1);
        /* Non-zero because it is a real enumerator, not the sentinel. */
        ROBO_CHECK(robo_css_property_computation_is_known(definition->computation) != 0);

        initial = robo_css_property_definition_initial_value(definition);
        ROBO_CHECK(initial != NULL);
        if (!initial) {
            continue;
        }
        /* ROBO_OK even for an INVALID answer: the call succeeding and the value
         * meaning nothing are two different facts, and this asserts the first. */
        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(definition->computation, initial,
                                                         &value, NULL),
                          ROBO_OK);
        /* The answer itself. INVALID is an answer, and the wrong one here. */
        ROBO_CHECK(value.kind != ROBO_CSS_COMPUTED_VALUE_INVALID);
        ROBO_CHECK(value.kind != ROBO_CSS_COMPUTED_VALUE_NONE);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_kind_is_known(value.kind), 1);
        ROBO_CHECK_STR_EQ(robo_css_computed_value_kind_name(value.kind),
                          index < sizeof(css_conformance_initials) /
                                      sizeof(css_conformance_initials[0])
                              ? css_conformance_initials[index].kind
                              : "unknown");
        text[0] = '\0';
        ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, sizeof(text), &length,
                                                        NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(text, index < sizeof(css_conformance_initials) /
                                    sizeof(css_conformance_initials[0])
                                ? css_conformance_initials[index].text
                                : "");
        robo_css_computed_value_dispose(&value);
    }

    robo_css_property_registry_destroy(registry);
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
static void test_css_conformance_registry_reference_initial(void)
{
    robo_css_property_registry_t *registry = NULL;
    static const char *const references[] = {
        "border-top-color", "border-right-color", "border-bottom-color",
        "border-left-color"
    };
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);

    for (index = 0u; index < sizeof(references) / sizeof(references[0]); index++) {
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_find(registry, references[index]);
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;
        char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
        size_t length = 0u;

        ROBO_CHECK(definition != NULL);
        if (!definition) {
            continue;
        }
        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              definition->computation,
                              robo_css_property_definition_initial_value(definition), &value,
                              NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(value.kind, ROBO_CSS_COMPUTED_VALUE_COLOR);
        /* The two facts together, because either alone would be ambiguous: a
         * resolved `currentColor` and an unresolved one are the same kind. */
        ROBO_CHECK_EQ_INT(value.unresolved, 1);
        ROBO_CHECK_EQ_INT(robo_css_color_is_current(value.color), 1);
        text[0] = '\0';
        ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, sizeof(text), &length,
                                                        NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(text, "currentColor");
        robo_css_computed_value_dispose(&value);
    }

    /* The control: a colour-shaped initial that is not a reference. */
    {
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_find(registry, "background-color");
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;

        ROBO_CHECK(definition != NULL);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              definition->computation,
                              robo_css_property_definition_initial_value(definition), &value,
                              NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(value.kind, ROBO_CSS_COMPUTED_VALUE_COLOR);
        ROBO_CHECK_EQ_INT(value.unresolved, 0);
        ROBO_CHECK_EQ_INT(robo_css_color_is_current(value.color), 0);
        robo_css_computed_value_dispose(&value);
    }

    robo_css_property_registry_destroy(registry);
}

/**
 * Every computation a registry row declares is one the normaliser knows.
 *
 * The enumerator's own contract, over the whole table rather than over the ten a
 * phase happened to add: a row carrying an enumerator the normaliser's switch
 * does not handle reaches its `default` and produces INVALID for every element
 * that declares the property — including the elements that declared it correctly.
 */
static void test_css_conformance_registry_computations_are_known(void)
{
    robo_css_property_registry_t *registry = NULL;
    size_t index;
    size_t known = 0u;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);

    for (index = 0u; index < robo_css_property_registry_count(registry); index++) {
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_at(registry, index);

        if (!definition) {
            continue;
        }
        /* Both doors, because they are two: the predicate and the name table. */
        if (robo_css_property_computation_is_known(definition->computation)) {
            known++;
            ROBO_CHECK(robo_css_property_computation_name(definition->computation) != NULL);
            ROBO_CHECK(robo_css_property_computation_name(definition->computation)[0] != '\0');
            /* And a *named* shape, never the fallback. */
            ROBO_CHECK(robo_css_property_computation_name(definition->computation)[0] != 'u' ||
                       strcmp(robo_css_property_computation_name(definition->computation),
                              "unknown") != 0);
        }
    }
    /* Every row, so a name table that had lost a case would show here as a count. */
    ROBO_CHECK_EQ_INT((int)known, (int)robo_css_property_registry_count(registry));

    /* The vocabulary is closed and bounded, walked in enumerator order: an
     * enumerator beyond COUNT is not one, and COUNT itself is not one either. */
    ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known(
                          (robo_css_property_computation_t)ROBO_CSS_PROPERTY_COMPUTATION_COUNT),
                      0);

    robo_css_property_registry_destroy(registry);
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
typedef struct css_conformance_declaration {
    const char *property;
    const char *declaration;
    const char *kind;
    const char *text;
} css_conformance_declaration_t;

static const css_conformance_declaration_t css_conformance_declarations[] = {
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
    {"background-repeat", "background-repeat: banana", "invalid",
     "not a background repeat value"},
    /* Borders (9.15) — the shape 9.15 added, and the one whose `none` is a value. */
    {"border-top-style", "border-top-style: solid", "border-style", "solid"},
    {"border-top-style", "border-top-style: none", "border-style", "none"},
    {"border-top-style", "border-top-style: hidden", "border-style", "hidden"},
    {"border-top-style", "border-top-style: banana", "invalid",
     "not a border style value"},
    {"border-top-color", "border-top-color: #00ff00", "colour", "#00ff00"},
    /* The font / text properties, so the matrix is the whole surface and not the
     * half Phase 9's later goals happen to have touched. */
    {"font-family", "font-family: Georgia, serif", "family-list", "Georgia, serif"},
    {"font-weight", "font-weight: bold", "keyword", "bold"},
    {"font-weight", "font-weight: 700", "number", "700"},
    {"letter-spacing", "letter-spacing: 2px", "length", "2px"},
    {"visibility", "visibility: hidden", "keyword", "hidden"},
    {"box-sizing", "box-sizing: border-box", "keyword", "border-box"}
};

/** A shape string for a computed value's diagnostic text. */
static void css_conformance_shape_of(robo_css_property_registry_t *registry,
                                     const css_conformance_declaration_t *row,
                                     char *out,
                                     size_t capacity,
                                     const char **out_kind)
{
    const robo_css_property_definition_t *definition =
        robo_css_property_registry_find(registry, row->property);
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    const robo_css_declaration_t *declaration;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    size_t length = 0u;

    out[0] = '\0';
    *out_kind = "<no property>";
    ROBO_CHECK(definition != NULL);
    if (!definition) {
        return;
    }
    /* Through the *real* declaration grammar, so the value under test is one the
     * tokenizer and parser produced rather than a hand-built tree. */
    ROBO_CHECK_EQ_INT(css_parse_declarations(row->declaration, &list, &error), ROBO_OK);
    declaration = robo_css_declaration_list_at(&list, 0u);
    ROBO_CHECK(declaration != NULL);
    if (declaration) {
        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              definition->computation,
                              robo_css_declaration_value(declaration), &value, NULL),
                          ROBO_OK);
        *out_kind = robo_css_computed_value_kind_name(value.kind);
        if (value.kind == ROBO_CSS_COMPUTED_VALUE_INVALID) {
            /* An INVALID value's text *is* its reason, which is the half of the
             * answer a developer reads. Asserting it means a refusal cannot
             * become a different refusal unnoticed. */
            robo_css_slice_t reason = robo_css_computed_value_text(&value);

            css_select_text(reason, out, capacity);
        } else if (robo_css_computed_value_write(&value, out, capacity, &length, NULL) !=
                   ROBO_OK) {
            out[0] = '\0';
        }
    }
    robo_css_computed_value_dispose(&value);
    robo_css_declaration_list_dispose(&list);
}

/**
 * The matrix: every supported property, one declaration, one exact answer.
 *
 * The kind is asserted beside the text on purpose. `width: 50%` printing as `50%`
 * is unremarkable; being a PERCENTAGE rather than a LENGTH is the contract, and a
 * test that asserted only the text would pass for a build that had started
 * resolving percentages into pixels.
 */
static void test_css_conformance_matrix_declarations(void)
{
    robo_css_property_registry_t *registry = NULL;
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);

    for (index = 0u; index < sizeof(css_conformance_declarations) /
                                sizeof(css_conformance_declarations[0]);
         index++) {
        const css_conformance_declaration_t *row = &css_conformance_declarations[index];
        char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
        const char *kind = NULL;

        css_conformance_shape_of(registry, row, text, sizeof(text), &kind);
        ROBO_CHECK_STR_EQ(kind, row->kind);
        ROBO_CHECK_STR_EQ(text, row->text);
    }

    robo_css_property_registry_destroy(registry);
}

/**
 * The matrix is a fact about the *product*, not about the normaliser.
 *
 * The table above drives `robo_css_computed_value_compute` directly. This one
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
static void test_css_conformance_matrix_through_the_pipeline(void)
{
    const size_t total = sizeof(css_conformance_declarations) /
                         sizeof(css_conformance_declarations[0]);
    size_t base;

    css_fixture();

    for (base = 0u; base < total; base += CSS_HOST_MAX_ELEMENTS) {
        css_inherit_fixture_t fixture;
        char sheet_text[4096];
        size_t offset = 0u;
        size_t count = total - base < CSS_HOST_MAX_ELEMENTS ? total - base
                                                            : CSS_HOST_MAX_ELEMENTS;
        size_t row_index;
        robo_css_element_ref_t refs[CSS_HOST_MAX_ELEMENTS];
        /*
         * The class names live here rather than in the loop below, and that is
         * load-bearing: a host element *borrows* the bytes it is given, so a name
         * held in a per-iteration local is a name dangling by the time the sheet
         * is added — which fails as a handful of elements silently matching
         * nothing, not as a crash.
         */
        char class_names[CSS_HOST_MAX_ELEMENTS][16];

        css_inherit_begin(&fixture);

        /*
         * One rule per row, each naming the element that carries it. Composing the
         * sheet from the same table the expectations come from is deliberate: a
         * second hand-written sheet would be a second thing to keep in step, and
         * the point of the test is that *this* table survives the pipeline.
         */
        for (row_index = 0u; row_index < count; row_index++) {
            char selector[32];

            (void)snprintf(class_names[row_index], sizeof(class_names[row_index]), "c%u",
                           (unsigned)row_index);
            (void)snprintf(selector, sizeof(selector), ".%s", class_names[row_index]);
            refs[row_index] = css_host_add(css_inherit_host(&fixture), "div", NULL,
                                           class_names[row_index]);
            css_shape_text(sheet_text, sizeof(sheet_text), &offset, selector);
            css_shape_text(sheet_text, sizeof(sheet_text), &offset, " { ");
            css_shape_text(sheet_text, sizeof(sheet_text), &offset,
                           css_conformance_declarations[base + row_index].declaration);
            css_shape_text(sheet_text, sizeof(sheet_text), &offset, " }\n");
        }
        ROBO_CHECK_EQ_INT(offset < sizeof(sheet_text), 1);
        css_host_seal(css_inherit_host(&fixture));
        css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, sheet_text);

        for (row_index = 0u; row_index < count; row_index++) {
            const css_conformance_declaration_t *row =
                &css_conformance_declarations[base + row_index];
            robo_css_computed_style_t *style = NULL;
            robo_css_error_t error = ROBO_CSS_CLEARED;

            ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
            /* A sheet of valid declarations produces no CSS *problem* even where
             * a declaration is refused: a refused value is an answer, not an
             * error, and the diagnostic rides on the computed value. */
            ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                                  css_inherit_context(&fixture), refs[row_index], style,
                                  &error, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
            /* The pipeline and the direct computation agree, text for text. */
            {
                char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

                css_computed_note(style, row->property, text, sizeof(text));
                ROBO_CHECK_STR_EQ(text, row->text);
            }
            /* Every one of them was *declared* on its element: a value that
             * reached the style by inheritance or by default would mean the rule
             * never matched. Asked as the flag rather than as the rendered set,
             * because a refused declaration is `declared+invalid` and a reference
             * is `declared+unresolved` — both declared, and the flag is the half
             * this test is about. */
            ROBO_CHECK_EQ_INT(
                robo_css_computed_style_has_flag(style, robo_css_slice_from_text(
                                                            row->property),
                                                 ROBO_CSS_COMPUTED_FLAG_DECLARED),
                1);
            /* And an INVALID declaration is counted as incomplete rather than
             * silently dropped, which is how a consumer sees how much of a style
             * it can trust. A clean declaration must *not* count, or the counter
             * would say nothing. */
            if (strcmp(row->kind, "invalid") == 0) {
                ROBO_CHECK(robo_css_computed_style_incomplete_count(style) > 0u);
                ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(style), 1);
            }
            robo_css_computed_style_destroy(style);
        }

        css_inherit_end(&fixture);
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
static void test_css_conformance_document_every_family(void)
{
    css_inherit_fixture_t fixture;
    robo_css_element_ref_t card;
    robo_css_element_ref_t label;
    robo_css_computed_style_t *card_style = NULL;
    robo_css_computed_style_t *label_style = NULL;
    robo_css_error_t error = ROBO_CSS_CLEARED;

    css_fixture();
    css_inherit_begin(&fixture);
    card = css_host_add(css_inherit_host(&fixture), "div", NULL, "card");
    label = css_host_add(css_inherit_host(&fixture), "span", NULL, "label");
    css_host_set_parent(css_inherit_host(&fixture), label, card);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, CSS_CONFORMANCE_SHEET);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&card_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&label_style, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                          css_inherit_context(&fixture), card, card_style, &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);

    /* Display and position: the two typed enumerator shapes. */
    css_computed_is(card_style, "display", "block");
    css_computed_is(card_style, "position", "relative");
    /* An inset and a dimenison, each keeping its own type. */
    css_computed_is(card_style, "top", "10px");
    css_computed_is(card_style, "width", "50%");
    css_computed_is(card_style, "height", "120px");
    css_computed_is(card_style, "z-index", "3");

    /* The `margin: 10px 20px` shorthand, distributed: vertical and horizontal. */
    css_computed_is(card_style, "margin-top", "10px");
    css_computed_is(card_style, "margin-right", "20px");
    css_computed_is(card_style, "margin-bottom", "10px");
    css_computed_is(card_style, "margin-left", "20px");
    /* `padding: 5px`, distributed evenly. */
    css_computed_is(card_style, "padding-top", "5px");
    css_computed_is(card_style, "padding-right", "5px");
    css_computed_is(card_style, "padding-bottom", "5px");
    css_computed_is(card_style, "padding-left", "5px");

    /* `border: 1px solid black`, distributed over three kinds and four sides. */
    css_computed_is(card_style, "border-top-width", "1px");
    css_computed_is(card_style, "border-right-width", "1px");
    css_computed_is(card_style, "border-bottom-width", "1px");
    css_computed_is(card_style, "border-left-width", "1px");
    css_computed_is(card_style, "border-top-style", "solid");
    css_computed_is(card_style, "border-right-style", "solid");
    css_computed_is(card_style, "border-bottom-style", "solid");
    css_computed_is(card_style, "border-left-style", "solid");
    css_computed_is(card_style, "border-top-color", "#000000");
    css_computed_is(card_style, "border-left-color", "#000000");

    /* The background, and the colour every `currentColor` would resolve against. */
    css_computed_is(card_style, "background-color", "#ffffff");
    css_computed_is(card_style, "color", "#336699");

    /* A declared value is `declared` and nothing else — not defaulted, not
     * inherited — for every family at once. */
    css_computed_flags_are(card_style, "display", "declared");
    css_computed_flags_are(card_style, "margin-left", "declared");
    css_computed_flags_are(card_style, "border-top-style", "declared");
    css_computed_flags_are(card_style, "background-color", "declared");
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
    css_computed_is(card_style, "border-top-left-radius", "<absent>");
    css_computed_is(card_style, "letter-spacing", "normal");
    css_computed_flags_are(card_style, "letter-spacing", "default");

    /* --- the child ------------------------------------------------------- */

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                          css_inherit_context(&fixture), label, label_style, &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);

    /* `color` is inherited, so the label has the card's *computed* colour. */
    css_computed_is(label_style, "color", "#336699");
    css_computed_flags_are(label_style, "color", "inherited");
    /* What it declared itself won. */
    css_computed_is(label_style, "border-top-style", "dashed");
    css_computed_flags_are(label_style, "border-top-style", "declared");
    css_computed_is(label_style, "margin-left", "4px");
    /* A non-inherited family leaves no entry at all, rather than a default that
     * looked like an inherited zero. */
    css_computed_is(label_style, "margin-top", "<absent>");
    css_computed_is(label_style, "padding-top", "<absent>");
    css_computed_is(label_style, "background-color", "<absent>");
    css_computed_is(label_style, "border-left-style", "<absent>");
    /* And an inherited property *is* present on the child, sourced from the
     * parent — the other half of the same rule. */
    css_computed_is(label_style, "letter-spacing", "normal");
    css_computed_flags_are(label_style, "letter-spacing", "inherited+default");
    /* The border colour is *not* inherited either — so `currentColor` on the
     * label is resolved against the label's own inherited `color` only when the
     * label actually declares a border colour. It does not, so there is no
     * entry, and the reference has nowhere to appear. */
    css_computed_is(label_style, "border-left-color", "<absent>");

    /* Nothing in a clean document is incomplete or truncated. */
    ROBO_CHECK_EQ_INT((int)robo_css_computed_style_incomplete_count(card_style), 0);
    ROBO_CHECK_EQ_INT((int)robo_css_computed_style_incomplete_count(label_style), 0);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_is_truncated(card_style), 0);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_is_truncated(label_style), 0);

    robo_css_computed_style_destroy(label_style);
    robo_css_computed_style_destroy(card_style);
    css_inherit_end(&fixture);
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
static void test_css_conformance_document_cascade_across_families(void)
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
    css_inherit_fixture_t fixture;
    css_inline_stub_t inline_stub;
    robo_css_element_ref_t by_id;
    robo_css_element_ref_t by_class;
    robo_css_element_ref_t by_important;
    robo_css_element_ref_t by_source_order;
    robo_css_element_ref_t by_inline;

    css_fixture();
    css_inherit_begin(&fixture);
    /*
     * One inline value for the whole document, and it deliberately names a
     * property the sheet *also* declares — `margin-top` — so the contest below
     * is a real one between the inline level and the author's type rule. It names
     * nothing else, so every `color` assertion above it is about the author's own
     * rules and the attribute is not accidentally standing in for them.
     */
    css_inherit_inline(&fixture, &inline_stub, "margin-top: 9px");

    by_id = css_host_add(css_inherit_host(&fixture), "div", "by-id", NULL);
    by_class = css_host_add(css_inherit_host(&fixture), "div", NULL, "by-class");
    by_important = css_host_add(css_inherit_host(&fixture), "div", NULL, "by-important");
    by_source_order = css_host_add(css_inherit_host(&fixture), "div", "later", NULL);
    by_inline = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, sheet);

    /* Specificity: the id beats the class beats the type. */
    {
        robo_css_computed_style_t *style = NULL;
        robo_css_error_t error = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                              css_inherit_context(&fixture), by_id, style, &error, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        /* The id rule wins the colour, and the *type* rule still wins a property
         * the id rule never mentioned. (`margin-top` is deliberately not asserted
         * here: the inline attribute of this fixture names it, and the stub is
         * per context, so it would be `9px` on every element of the document. The
         * type rule's reach is shown by `border-top-style` instead.) */
        css_computed_is(style, "color", "#222222");
        css_computed_is(style, "border-top-style", "dashed");
        /* Three rules cover `border-top-style` and only the last `div` one
         * matches this element, so `dashed`. `#later`'s `dotted` belongs to the
         * element whose id *is* `later`, which is asserted just below — and that
         * pairing is the point: an id rule first in the sheet still beats a type
         * rule after it. */
        css_computed_is(style, "border-top-style", "dashed");
        robo_css_computed_style_destroy(style);
    }
    /* Specificity beats source order, and the losing rule is later in the sheet. */
    {
        robo_css_computed_style_t *style = NULL;

        ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                              css_inherit_context(&fixture), by_source_order, style, NULL,
                              NULL),
                          ROBO_OK);
        css_computed_is(style, "color", "#777777");
        css_computed_is(style, "border-top-style", "dotted");
        robo_css_computed_style_destroy(style);
    }
    /* Source order among equals: the second `.by-class` rule wins the colour. */
    {
        robo_css_computed_style_t *style = NULL;

        ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                              css_inherit_context(&fixture), by_class, style, NULL, NULL),
                          ROBO_OK);
        css_computed_is(style, "color", "#444444");
        robo_css_computed_style_destroy(style);
    }
    /* Importance: the `!important` declaration outranks the later one. */
    {
        robo_css_computed_style_t *style = NULL;

        ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                              css_inherit_context(&fixture), by_important, style, NULL, NULL),
                          ROBO_OK);
        css_computed_is(style, "color", "#555555");
        robo_css_computed_style_destroy(style);
    }
    /*
     * The inline level, and the two halves of what a level *is*: the attribute
     * beats the author's rule on the property it names, and the author's rule
     * still applies to every property it does not. A cascade that let the
     * attribute replace the style would fail the second half, and one that
     * ignored it would fail the first.
     */
    {
        robo_css_computed_style_t *style = NULL;

        ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                              css_inherit_context(&fixture), by_inline, style, NULL, NULL),
                          ROBO_OK);
        css_computed_is(style, "margin-top", "9px");
        css_computed_flags_are(style, "margin-top", "declared");
        /* Not named by the attribute, so the type rule's answer stands. */
        css_computed_is(style, "color", "#111111");
        css_computed_is(style, "border-top-style", "dashed");
        robo_css_computed_style_destroy(style);
    }
    /* The stub was asked, once per element the context computed. */
    ROBO_CHECK_EQ_INT(inline_stub.calls >= 5, 1);

    css_inherit_end(&fixture);
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
static void test_css_conformance_document_shorthand_and_longhand(void)
{
    static const struct {
        const char *label;        /* the property the answer is read from */
        const char *longhand_last;
        const char *shorthand_last;
        const char *longhand_value;   /* what the longhand's own declaration says */
        const char *shorthand_value;  /* what the shorthand gives that property */
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

    css_fixture();

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        css_inherit_fixture_t fixture;
        robo_css_element_ref_t longhand_last;
        robo_css_element_ref_t shorthand_last;
        robo_css_computed_style_t *style = NULL;

        css_inherit_begin(&fixture);
        longhand_last = css_host_add(css_inherit_host(&fixture), "div", NULL, "a");
        shorthand_last = css_host_add(css_inherit_host(&fixture), "div", NULL, "b");
        css_host_seal(css_inherit_host(&fixture));
        {
            char sheet_text[512];
            size_t offset = 0u;

            css_shape_text(sheet_text, sizeof(sheet_text), &offset, "div.a { ");
            css_shape_text(sheet_text, sizeof(sheet_text), &offset, cases[index].longhand_last);
            css_shape_text(sheet_text, sizeof(sheet_text), &offset, " }\n");
            css_shape_text(sheet_text, sizeof(sheet_text), &offset, "div.b { ");
            css_shape_text(sheet_text, sizeof(sheet_text), &offset,
                           cases[index].shorthand_last);
            css_shape_text(sheet_text, sizeof(sheet_text), &offset, " }\n");
            css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, sheet_text);
        }

        /* Longhand written last: it wins. */
        ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                              css_inherit_context(&fixture), longhand_last, style, NULL, NULL),
                          ROBO_OK);
        css_computed_is(style, cases[index].label, cases[index].longhand_value);
        robo_css_computed_style_destroy(style);

        /* Shorthand written last: it wins, and that is the whole rule. */
        style = NULL;
        ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                              css_inherit_context(&fixture), shorthand_last, style, NULL,
                              NULL),
                          ROBO_OK);
        css_computed_is(style, cases[index].label, cases[index].shorthand_value);
        robo_css_computed_style_destroy(style);

        css_inherit_end(&fixture);
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
static void test_css_conformance_document_three_declarers(void)
{
    css_inherit_fixture_t fixture;
    robo_css_element_ref_t div;
    robo_css_computed_style_t *style = NULL;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div {\n"
                      "    border: 1px solid #111111;\n"
                      "    border-top: 2px dashed;\n"
                      "    border-top-width: 4px;\n"
                      "}\n");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                          css_inherit_context(&fixture), div, style, NULL, NULL),
                      ROBO_OK);

    /* The top edge is what the three declarations together say: the width from
     * the longhand, the style from the side shorthand, the colour from `border`
     * — which is because `border-top: 2px dashed` *reset* the colour it did not
     * name, and then `border` no longer wins that longhand. */
    css_computed_is(style, "border-top-width", "4px");
    css_computed_is(style, "border-top-style", "dashed");
    /* The other three sides are untouched by either later declaration. */
    css_computed_is(style, "border-right-width", "1px");
    css_computed_is(style, "border-bottom-width", "1px");
    css_computed_is(style, "border-left-width", "1px");
    css_computed_is(style, "border-right-style", "solid");
    css_computed_is(style, "border-bottom-style", "solid");
    css_computed_is(style, "border-left-style", "solid");
    css_computed_is(style, "border-right-color", "#111111");
    css_computed_is(style, "border-bottom-color", "#111111");
    css_computed_is(style, "border-left-color", "#111111");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
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
 * every door which promises ROBO_OK keeps that promise, the style still exists,
 * and the allocator comes back to where it started.
 */
static const char *const css_conformance_malformed[] = {
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
static void test_css_conformance_pipeline_malformed_corpus(void)
{
    size_t index;

    css_fixture();

    for (index = 0u; index < sizeof(css_conformance_malformed) /
                                sizeof(css_conformance_malformed[0]);
         index++) {
        css_inherit_fixture_t fixture;
        robo_css_element_ref_t div;
        robo_css_computed_style_t *style = NULL;
        robo_css_error_t error = ROBO_CSS_CLEARED;
        int enabled = css_allocator_ensure_enabled();
        size_t baseline = robo_mem_debug_live_count();

        css_inherit_begin(&fixture);
        div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
        css_host_seal(css_inherit_host(&fixture));
        /* A sheet that cannot parse is still a sheet: the parse reports and
         * returns ROBO_OK, and the rule list holds what survived. */
        css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                          css_conformance_malformed[index]);

        ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
        /* The one door that must still work on any document. */
        ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                              css_inherit_context(&fixture), div, style, &error, NULL),
                          ROBO_OK);
        /* A style an author can read something out of, whatever the input was. */
        ROBO_CHECK(style != NULL);
        if (style) {
            size_t incomplete = robo_css_computed_style_incomplete_count(style);

            /* The counter is a count of entries, so it can never exceed them,
             * and `has_invalid` agrees with it: the two are derived from the same
             * flags and a disagreement would mean one of them was lying. */
            ROBO_CHECK(incomplete <= robo_css_computed_style_count(style));
            if (robo_css_computed_style_has_invalid(style)) {
                ROBO_CHECK(incomplete > 0u);
            }
            /*
             * The query door still answers, on a document that may have parsed to
             * nothing at all — and the answer is the invariant, not its
             * provenance. `color` is inherited, so on a document with no readable
             * declaration it arrives from the registry's default (`from_initial`
             * 0, because an inherited property always *has* an entry), while on
             * one whose declaration survived it is that declaration's answer. Both
             * are correct; what must never happen is a failure code or an empty
             * value.
             */
            {
                robo_css_computed_value_t value = ROBO_CSS_CLEARED;
                int from_initial = -1;
                robo_result_t rc = robo_css_style_context_query_computed_value(
                    css_inherit_context(&fixture), style, robo_css_slice_from_text("color"),
                    &value, &from_initial, NULL);

                ROBO_CHECK_EQ_INT(rc, ROBO_OK);
                ROBO_CHECK_EQ_INT(from_initial == 0 || from_initial == 1, 1);
                ROBO_CHECK_EQ_INT(value.kind != ROBO_CSS_COMPUTED_VALUE_NONE, 1);
                /* An answer that came from the registry's initial is a colour,
                 * because `color`'s initial value is one. */
                if (from_initial == 1) {
                    ROBO_CHECK_EQ_INT(value.kind, ROBO_CSS_COMPUTED_VALUE_COLOR);
                }
                robo_css_computed_value_dispose(&value);
            }
            robo_css_computed_style_destroy(style);
        }
        css_inherit_end(&fixture);

        /* Nothing leaked, for every one of the forty-two inputs. A malformed
         * document that leaked would be a malformed document that eventually
         * exhausted memory. */
        ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        css_allocator_restore(enabled);
    }
}

/**
 * Identical bytes, identical styles — asserted over the *whole* style.
 *
 * Per-family suites assert determinism of one value. This asserts it of the
 * *style*: two independent contexts, the same sheet and the same tree, and every
 * entry of the second equal to the corresponding entry of the first — by
 * `robo_css_computed_value_equals` rather than by text, so a difference in
 * formatting could not hide a difference in value and vice versa.
 */
static void test_css_conformance_pipeline_determinism(void)
{
    css_inherit_fixture_t first;
    css_inherit_fixture_t second;
    robo_css_element_ref_t first_div;
    robo_css_element_ref_t second_div;
    robo_css_computed_style_t *first_style = NULL;
    robo_css_computed_style_t *second_style = NULL;
    size_t index;

    css_fixture();
    css_inherit_begin(&first);
    css_inherit_begin(&second);
    first_div = css_host_add(css_inherit_host(&first), "div", NULL, "card");
    second_div = css_host_add(css_inherit_host(&second), "div", NULL, "card");
    css_host_seal(css_inherit_host(&first));
    css_host_seal(css_inherit_host(&second));
    css_inherit_sheet(&first, ROBO_CSS_ORIGIN_AUTHOR, CSS_CONFORMANCE_SHEET);
    css_inherit_sheet(&second, ROBO_CSS_ORIGIN_AUTHOR, CSS_CONFORMANCE_SHEET);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&first_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&second_style, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                          css_inherit_context(&first), first_div, first_style, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                          css_inherit_context(&second), second_div, second_style, NULL, NULL),
                      ROBO_OK);

    /* The same number of entries, in the same order, under the same names. */
    ROBO_CHECK_EQ_INT((int)robo_css_computed_style_count(first_style),
                      (int)robo_css_computed_style_count(second_style));
    for (index = 0u; index < robo_css_computed_style_count(first_style); index++) {
        const robo_css_computed_value_t *a =
            robo_css_computed_style_computed_at(first_style, index);
        const robo_css_computed_value_t *b =
            robo_css_computed_style_computed_at(second_style, index);
        robo_css_slice_t name = robo_css_computed_style_name_at(first_style, index);
        robo_css_slice_t other = robo_css_computed_style_name_at(second_style, index);
        const robo_css_value_t *specified_a;
        const robo_css_value_t *specified_b;

        /* The same *name* at the same index, so the comparison below is about
         * one property and not about two lists that happened to line up. */
        ROBO_CHECK_EQ_INT(name.length, other.length);
        ROBO_CHECK_EQ_INT(name.length > 0u, 1);
        specified_a = robo_css_computed_style_specified_by_name(first_style, name);
        ROBO_CHECK(specified_a != NULL);
        /* The value is equal by structure, not by text: a difference in
         * formatting could not hide a difference in value, or the reverse. */
        ROBO_CHECK_EQ_INT(robo_css_computed_value_equals(a, b), 1);
        /* And the specified half agrees too, so a difference that only the
         * normaliser papered over would still show. */
        specified_b = robo_css_computed_style_specified_by_name(second_style, other);
        ROBO_CHECK_EQ_INT(specified_b != NULL, 1);
    }
    ROBO_CHECK(robo_css_computed_style_count(first_style) > 0u);

    robo_css_computed_style_destroy(second_style);
    robo_css_computed_style_destroy(first_style);
    css_inherit_end(&second);
    css_inherit_end(&first);
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
static void test_css_conformance_pipeline_tokenizer_to_computed(void)
{
    static const char *const text =
        "div.card {\n"
        "    color: #336699;   /* the identifier and the hash */\n"
        "    margin: 10px 20px;\n"
        "}\n";
    css_inherit_fixture_t fixture;
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    robo_css_stylesheet_id_t sheet;
    robo_css_element_ref_t div;
    robo_css_computed_style_t *style = NULL;

    css_fixture();

    /* --- 1. the tokenizer, over the very bytes the sheet will be given --- */
    css_source_of_string(&source, text);
    (void)css_lex(&source, &log);
    ROBO_CHECK_EQ_INT(log.eof_seen, 1);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    /* The invariant the whole chain rests on: the tokens tile the source. */
    ROBO_CHECK_EQ_INT(css_log_tiles(&log, robo_css_source_length(&source)), 1);
    /* A comment is one token and its bytes are the source's, so the parser below
     * sees the same stream this test just read. */
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_COMMENT) == 1u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_HASH) == 1u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_IDENT) > 0u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_DIMENSION) == 2u);

    /* --- 2. the parser, and the same tokens on the other side --- */
    sheet = css_parse_sheet(text, NULL);
    ROBO_CHECK_EQ_INT((int)robo_css_stylesheet_rule_count(sheet), 1);
    {
        const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, 0u);

        ROBO_CHECK(rule != NULL);
        /* Two declarations, and the comment between them did not become a third
         * — nor did it attach itself to either value. */
        ROBO_CHECK_EQ_INT((int)robo_css_style_rule_declaration_count(rule), 2);
        ROBO_CHECK_EQ_INT(css_parse_declaration_is(rule, 0u, "color"), 1);
        ROBO_CHECK_EQ_INT(css_parse_declaration_is(rule, 1u, "margin"), 1);
        /* The value the grammar built is the *tokenizer's* tokens: the hash the
         * log found is the hash this value holds, spelled the same way. */
        {
            char shape[128];

            css_declaration_shape(robo_css_style_rule_declarations(rule), 0u, shape,
                                  sizeof(shape));
            ROBO_CHECK_STR_EQ(shape, "#336699");
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

        css_prelude_shape(css_sheet_style_rule(sheet, 0u), prelude, sizeof(prelude));
        ROBO_CHECK_STR_EQ(prelude, "div,.,card");
    }

    /* --- 3. selectors, and the specificity the cascade will order by --- */
    {
        robo_css_selector_list_t list = ROBO_CSS_CLEARED;
        robo_css_specificity_t specificity;

        css_select_parse("div.card", &list);
        ROBO_CHECK_EQ_INT((int)robo_css_selector_list_count(&list), 1);
        specificity = robo_css_specificity_of_list_item(&list, 0u);
        /* One class and one type: (0, 1, 1). */
        ROBO_CHECK_EQ_INT(specificity.id, 0u);
        ROBO_CHECK_EQ_INT(specificity.class_like, 1u);
        ROBO_CHECK_EQ_INT(specificity.type_like, 1u);
        robo_css_selector_list_dispose(&list);
    }

    /* --- 4. the cascade, over the sheet the parser produced --- */
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, "card");
    css_host_seal(css_inherit_host(&fixture));
    /* The same sheet id the parse above returned, so this is the *parsed* rules
     * travelling rather than a re-parse of the same text. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_origin(sheet, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_OK);
    {
        size_t rule_index;

        for (rule_index = 0u; rule_index < robo_css_stylesheet_rule_count(sheet);
             rule_index++) {
            robo_css_style_rule_t *rule = (robo_css_style_rule_t *)robo_css_rule_style(
                robo_css_stylesheet_rule(sheet, rule_index));
            robo_css_error_t rule_error = ROBO_CSS_CLEARED;

            if (rule) {
                ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors(
                                      rule, robo_css_stylesheet_source(sheet), &rule_error,
                                      NULL),
                                  ROBO_OK);
            }
        }
    }
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(fixture.cascade.context,
                                                      ROBO_CSS_ORIGIN_AUTHOR, sheet, NULL),
                      ROBO_OK);
    {
        robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
        char winner[64];

        ROBO_CHECK_EQ_INT(robo_css_style_context_match_declarations(
                              fixture.cascade.context, div, &cascade, NULL, NULL),
                          ROBO_OK);
        /* The cascade found the rule, and the selector it matched is the class
         * one — so the specificity read above is the specificity it used. */
        ROBO_CHECK(robo_css_cascade_count(&cascade) > 0u);
        ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", winner,
                                                  sizeof(winner)),
                          ROBO_CSS_CASCADE_VERDICT_DECIDED);
        ROBO_CHECK_STR_EQ(winner, "#336699");
        robo_css_cascade_dispose(&cascade);
    }

    /* --- 5. inheritance, and the computed style it produces --- */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                          fixture.cascade.context, div, style, NULL, NULL),
                      ROBO_OK);
    /* Both declaration kinds arrive typed, and the shorthand was distributed by
     * the same pass every other test in the build goes through. */
    css_computed_is(style, "color", "#336699");
    css_computed_is(style, "margin-top", "10px");
    css_computed_is(style, "margin-right", "20px");
    css_computed_flags_are(style, "color", "declared");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
    /* The sheet this test created belongs to the fixture-free part above, so it
     * is released here rather than left in the module's fixed table. */
    (void)robo_css_stylesheet_destroy(sheet, NULL);
    robo_css_source_dispose(&source);
}

/**
 * The whole pipeline returns the allocator to where it started.
 *
 * Every stage in this chain owns something — a source copies bytes, a sheet owns
 * rules, a cascade owns candidates, a resolved style owns value trees, a computed
 * style owns values and their text. A build that leaked at any one of them would
 * still pass every value assertion in the file.
 */
static void test_css_conformance_pipeline_lifetime(void)
{
    css_inherit_fixture_t fixture;
    int enabled = css_allocator_ensure_enabled();
    size_t baseline;
    size_t round;

    css_fixture();
    baseline = robo_mem_debug_live_count();

    for (round = 0u; round < 8u; round++) {
        robo_css_element_ref_t div;
        robo_css_computed_style_t *style = NULL;

        css_inherit_begin(&fixture);
        div = css_host_add(css_inherit_host(&fixture), "div", NULL, "card");
        css_host_seal(css_inherit_host(&fixture));
        css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, CSS_CONFORMANCE_SHEET);
        ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                              css_inherit_context(&fixture), div, style, NULL, NULL),
                          ROBO_OK);
        css_computed_is(style, "color", "#336699");
        /* The query door allocates its answer too, and it has to be released by
         * the caller: a value nothing disposes is the leak this catches. Asked
         * for a property this element genuinely has no entry for — non-inherited,
         * and not one the sheet names — so the answer really is the registry's
         * initial value rather than an inherited one. */
        {
            robo_css_computed_value_t value = ROBO_CSS_CLEARED;
            int from_initial = -1;

            ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                                  css_inherit_context(&fixture), style,
                                  robo_css_slice_from_text("background-image"), &value,
                                  &from_initial, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(from_initial, 1);
            ROBO_CHECK_EQ_INT(value.kind, ROBO_CSS_COMPUTED_VALUE_KEYWORD);
            robo_css_computed_value_dispose(&value);
        }
        robo_css_computed_style_destroy(style);
        css_inherit_end(&fixture);

        /* After every round, not only at the end: a leak that happened only on
         * the first iteration would be invisible in a single end-of-loop check. */
        ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    }

    /* And the same document parsed fresh each time is the same document. */
    css_allocator_restore(enabled);
}

/* ------------------------------------------------------------------ */
/* The build's promises                                                */
/* ------------------------------------------------------------------ */

/**
 * Every flag in the capability struct, asserted by name.
 *
 * This is the phase closing the one hole its own documentation left. 9.14 and
 * 9.15 each recorded that `robo_css_get_capabilities` reports `backgrounds` and
 * `borders` as 1, and **neither flag was ever asserted anywhere in the build** —
 * `css.capabilities` stops at `positioning` and then jumps to the two flags that
 * are 0. A documented promise with no test behind it is a promise that can stop
 * being true silently, which is exactly what a conformance phase exists to find.
 *
 * The list is written out rather than derived from the struct, so a field added
 * later is a deliberate act here and a *missing* field shows as a flag nothing
 * asserts. The count is pinned against the struct's own size for the same reason.
 */
static void test_css_conformance_capabilities_every_flag(void)
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
        {"parser", 1},              {"inline_styles", 1},
        {"selector_parsing", 1},    {"selector_matching", 1},
        {"cascade", 1},             {"specificity", 1},
        {"inheritance", 1},         {"computed_values", 1},
        {"colors", 1},              {"units", 1},
        {"box_model", 1},           {"display", 1},
        {"positioning", 1},         {"backgrounds", 1},
        {"borders", 1},             {"default_styles", 0},
        {"stylesheet_loading", 0}
    };
    robo_css_capabilities_t caps;
    int *as_ints = (int *)&caps;
    size_t index;

    ROBO_CHECK_EQ_INT((int)(sizeof(flags) / sizeof(flags[0])),
                      (int)(sizeof(robo_css_capabilities_t) / sizeof(int)));

    ROBO_CHECK_EQ_INT(robo_css_get_capabilities(&caps), ROBO_OK);
    for (index = 0u; index < sizeof(flags) / sizeof(flags[0]); index++) {
        /*
         * The flags are `int` fields in declaration order with no padding, which
         * the size assertion above pins; reading them positionally is what lets
         * one list cover the struct without a second copy of every member's name.
         */
        if (as_ints[index] != flags[index].expected) {
            robo_test_fail(__FILE__, __LINE__, "caps.%s == %d (got %d)", flags[index].name,
                           flags[index].expected, as_ints[index]);
        }
    }

    /* The two that were never asserted, asserted the other way round as well: a
     * flag that is 1 is a promise the *suite* keeps, so the families this phase
     * exercises and the families the build claims are the same set. */
    ROBO_CHECK_EQ_INT(caps.backgrounds, 1);
    ROBO_CHECK_EQ_INT(caps.borders, 1);

    /* The frozen-surface flag is a promise about the phase, not a capability. */
    ROBO_CHECK_EQ_INT(ROBO_CSS_API_FROZEN, 0);
    ROBO_CHECK_EQ_INT(ROBO_CSS_VERSION_MAJOR, 0);
    ROBO_CHECK_EQ_INT(ROBO_CSS_VERSION_MINOR, 16);
}

void robo_test_register_css_conformance(void);

void robo_test_register_css_conformance(void)
{
    robo_test_register("css.conformance.registry.every_initial_computes",
                       test_css_conformance_registry_every_initial_computes);
    robo_test_register("css.conformance.registry.reference_initial",
                       test_css_conformance_registry_reference_initial);
    robo_test_register("css.conformance.registry.computations_are_known",
                       test_css_conformance_registry_computations_are_known);
    robo_test_register("css.conformance.matrix.declarations",
                       test_css_conformance_matrix_declarations);
    robo_test_register("css.conformance.matrix.through_the_pipeline",
                       test_css_conformance_matrix_through_the_pipeline);
    robo_test_register("css.conformance.document.every_family",
                       test_css_conformance_document_every_family);
    robo_test_register("css.conformance.document.cascade_across_families",
                       test_css_conformance_document_cascade_across_families);
    robo_test_register("css.conformance.document.shorthand_and_longhand",
                       test_css_conformance_document_shorthand_and_longhand);
    robo_test_register("css.conformance.document.three_declarers",
                       test_css_conformance_document_three_declarers);
    robo_test_register("css.conformance.pipeline.tokenizer_to_computed",
                       test_css_conformance_pipeline_tokenizer_to_computed);
    robo_test_register("css.conformance.pipeline.malformed_corpus",
                       test_css_conformance_pipeline_malformed_corpus);
    robo_test_register("css.conformance.pipeline.determinism",
                       test_css_conformance_pipeline_determinism);
    robo_test_register("css.conformance.pipeline.lifetime",
                       test_css_conformance_pipeline_lifetime);
    robo_test_register("css.conformance.capabilities.every_flag",
                       test_css_conformance_capabilities_every_flag);
}
