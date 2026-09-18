/*
 * RoboCSS unit and number suite (Phase 9.10) — the vocabulary, resolution, the
 * typed numeric value, and what the computed-value layer makes of it.
 *
 * Four groups, in the order the module is built:
 *
 *   the set      the closed unit vocabulary: every enumerator reachable by its
 *                spelling, the four families, the fixed factors, the viewport
 *                each viewport unit measures
 *   resolution   absolute conversion, and the four answers a *relative* unit can
 *                get — including the one that matters most, NEEDS_CONTEXT
 *   the value    the four numeric kinds, equality, integer conversion, the five
 *                parse outcomes, and the canonical spelling
 *   integration  numbers and units through declarations, the computed-value
 *                layer, inheritance and the real DOM
 *
 * Values are reached through *declaration text* wherever a declaration can say
 * the same thing, for the reason the colour suite does the same: a stylesheet
 * takes the tokenizer's route, and a hand-built token would prove the parse
 * works on input the tokenizer never produces. The two exceptions are the
 * resolver and the record, which are asked directly because no stylesheet can
 * call them.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Reading a numeric value out of a declaration                        */
/* ------------------------------------------------------------------ */

/**
 * The specified value of the single declaration in @p text.
 *
 * @p list is filled and owns nothing here (declarations borrow the text), so the
 * caller must keep @p text alive for as long as it reads the value — which is why
 * every caller passes a literal.
 */
static const robo_css_value_t *css_unit_value_of(const char *text,
                                                robo_css_declaration_list_t *list)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    css_fixture();
    ROBO_CHECK_EQ_INT(css_parse_declarations(text, list, &error), ROBO_OK);
    /* One declaration out: a fixture that produced none would make every
     * expectation built on it an assertion about a NULL value. */
    ROBO_CHECK_EQ_INT((int)robo_css_declaration_list_count(list), 1);
    return robo_css_declaration_value(robo_css_declaration_list_at(list, 0u));
}

/**
 * Parse the numeric value in @p declaration, returning what came out.
 *
 * `robo_css_numeric_t` is a tag, a double and an enumerator by value, so it is
 * safe to read after the list is disposed — which is why this does not leak the
 * list into every assertion's scope.
 */
static robo_css_numeric_parse_t css_numeric_parse(const char *declaration,
                                                 robo_css_numeric_t *out)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value = css_unit_value_of(declaration, &list);
    robo_css_numeric_parse_t outcome;

    ROBO_CHECK(value != NULL);
    outcome = robo_css_numeric_from_value(value, out);
    robo_css_declaration_list_dispose(&list);
    return outcome;
}

/** Assert @p declaration is a numeric value and writes back as @p expected. */
static void css_numeric_is(const char *declaration, const char *expected)
{
    robo_css_numeric_t numeric = robo_css_numeric_none();
    char text[ROBO_CSS_NUMERIC_TEXT_MAX];
    size_t length = 0u;

    ROBO_CHECK_EQ_INT((int)css_numeric_parse(declaration, &numeric),
                      ROBO_CSS_NUMERIC_PARSE_RESOLVED);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, text, sizeof(text), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(text, expected);
    /* The length is asserted too: a writer that returned the right bytes and the
     * wrong count would pass the comparison above and break every caller that
     * trusts the count. */
    ROBO_CHECK_EQ_INT(length, strlen(expected));
}

/** The kind name @p declaration parses to. */
static const char *css_numeric_kind_of(const char *declaration)
{
    robo_css_numeric_t numeric = robo_css_numeric_none();
    robo_css_numeric_parse_t outcome = css_numeric_parse(declaration, &numeric);
    const char *name;

    if (outcome != ROBO_CSS_NUMERIC_PARSE_RESOLVED) {
        return "not numeric";
    }
    name = robo_css_numeric_kind_name(numeric.kind);
    /* The returned name is one of the module's static strings, so it outlives
     * this call — which is what makes a compare-against-a-literal safe here. */
    return name;
}

/** The outcome name @p declaration parses to. */
static const char *css_numeric_outcome_of(const char *declaration)
{
    robo_css_numeric_t numeric = robo_css_numeric_none();

    return robo_css_numeric_parse_name(css_numeric_parse(declaration, &numeric));
}

/** Non-zero when @p a and @p b are within @p epsilon of each other. */
static int css_unit_near(double a, double b, double epsilon)
{
    double difference = a - b;

    if (difference < 0.0) {
        difference = -difference;
    }
    return difference <= epsilon;
}

/**
 * A magnitude beyond what a `double` can hold.
 *
 * Built by multiplication rather than written as a literal so the *runtime* is
 * what overflows: the module's finiteness guard is about values arriving from a
 * tokenizer, and a constant the compiler folded would test the compiler's
 * arithmetic instead. Not `INFINITY` from `<math.h>`, because this suite stays
 * free of the freestanding-hostile headers the runtime itself avoids.
 */
static double css_unit_overflow(void)
{
    double value = 1.0;
    int step;

    for (step = 0; step < 400; step++) {
        value *= 10.0;
    }
    return value;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * Every unit in the table, reachable by its spelling and back again.
 *
 * The round trip is the assertion that matters: it is what proves there is one
 * row per unit and that nothing is registered twice. A table whose `name` and
 * `unit` columns disagreed would satisfy a per-unit check that asked each
 * enumerator for its name and stopped there.
 */
static void test_unit_vocabulary(void)
{
    int unit;
    int distinct = 0;

    css_fixture();

    for (unit = (int)ROBO_CSS_UNIT_NONE; unit < (int)ROBO_CSS_UNIT_COUNT; unit++) {
        robo_css_unit_t enumerator = (robo_css_unit_t)unit;
        const char *name = robo_css_unit_name(enumerator);

        ROBO_CHECK_EQ_INT(robo_css_unit_is_known(enumerator), 1);
        if (enumerator == ROBO_CSS_UNIT_NONE) {
            /* NONE is a defined enumerator whose spelling is empty, because it is
             * the absence of a unit rather than one nobody knows. */
            ROBO_CHECK_STR_EQ(name, "");
            ROBO_CHECK_EQ_INT(robo_css_unit_class(enumerator), ROBO_CSS_UNIT_CLASS_NONE);
            continue;
        }
        ROBO_CHECK(name[0] != '\0');
        ROBO_CHECK_EQ_INT(robo_css_unit_from_text(name), enumerator);
        distinct++;
    }
    /* 31 real units plus NONE. Stated as a number so that adding a row without
     * adding a test that walks to it fails here rather than passing quietly. */
    ROBO_CHECK_EQ_INT(distinct, 31);

    /* The families, one representative each, and the boundary that makes a unit
     * a length at all. */
    ROBO_CHECK_EQ_INT(robo_css_unit_class(ROBO_CSS_UNIT_PX),
                      ROBO_CSS_UNIT_CLASS_ABSOLUTE);
    ROBO_CHECK_EQ_INT(robo_css_unit_class(ROBO_CSS_UNIT_EM),
                      ROBO_CSS_UNIT_CLASS_FONT_RELATIVE);
    ROBO_CHECK_EQ_INT(robo_css_unit_class(ROBO_CSS_UNIT_VW),
                      ROBO_CSS_UNIT_CLASS_VIEWPORT);
    ROBO_CHECK_EQ_INT(robo_css_unit_class(ROBO_CSS_UNIT_NONE), ROBO_CSS_UNIT_CLASS_NONE);
    ROBO_CHECK_STR_EQ(robo_css_unit_class_name(ROBO_CSS_UNIT_CLASS_ABSOLUTE), "absolute");
    ROBO_CHECK_STR_EQ(robo_css_unit_class_name(ROBO_CSS_UNIT_CLASS_FONT_RELATIVE),
                      "font-relative");
    ROBO_CHECK_STR_EQ(robo_css_unit_class_name(ROBO_CSS_UNIT_CLASS_VIEWPORT), "viewport");
    ROBO_CHECK_STR_EQ(robo_css_unit_class_name(ROBO_CSS_UNIT_CLASS_NONE), "none");
    ROBO_CHECK_STR_EQ(robo_css_unit_class_name((robo_css_unit_class_t)99), "none");

    /* An unknown enumerator is not known, and answers nothing rather than
     * pointing at the first row. */
    ROBO_CHECK_EQ_INT(robo_css_unit_is_known((robo_css_unit_t)99), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_known((robo_css_unit_t)-1), 0);
    ROBO_CHECK_STR_EQ(robo_css_unit_name((robo_css_unit_t)99), "");
    ROBO_CHECK_EQ_INT(robo_css_unit_class((robo_css_unit_t)99), ROBO_CSS_UNIT_CLASS_NONE);
    ROBO_CHECK_EQ_INT(robo_css_unit_viewport_family((robo_css_unit_t)99),
                      ROBO_CSS_VIEWPORT_DEFAULT);

    /* The identifier door: ASCII case-insensitive, whole-spelling, and empty for
     * nothing. `10pixels` is *not* `px`, which a prefix match would allow. */
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text("PX"), ROBO_CSS_UNIT_PX);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text("Em"), ROBO_CSS_UNIT_EM);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text("REM"), ROBO_CSS_UNIT_REM);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text("vMiN"), ROBO_CSS_UNIT_VMIN);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text("DVH"), ROBO_CSS_UNIT_DVH);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text("pixels"), ROBO_CSS_UNIT_NONE);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text(""), ROBO_CSS_UNIT_NONE);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text(NULL), ROBO_CSS_UNIT_NONE);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_slice(robo_css_slice_from_text("pt")),
                      ROBO_CSS_UNIT_PT);

    /* Which units have a fixed factor is a question about the *table*, not about
     * `to_px` — the distinction that a relative unit reported as absolute would
     * erase, and with it the whole "do not invent a font size" decision. */
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_PX), 1);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_IN), 1);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_Q), 1);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_EM), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_CH), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_LH), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_RLH), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_VW), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_DVMAX), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_NONE), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute((robo_css_unit_t)99), 0);

    /* Sixteen viewport units: four measures across four families. Counted rather
     * than listed so that a family added without its units fails here. */
    {
        int viewport = 0;
        int families[ROBO_CSS_VIEWPORT_FAMILY_COUNT] = { 0, 0, 0, 0 };

        for (unit = (int)ROBO_CSS_UNIT_NONE; unit < (int)ROBO_CSS_UNIT_COUNT; unit++) {
            robo_css_unit_t enumerator = (robo_css_unit_t)unit;

            if (robo_css_unit_is_viewport(enumerator)) {
                viewport++;
                families[(int)robo_css_unit_viewport_family(enumerator)]++;
            }
        }
        /* Six units in the default family — `vw`, `vh`, `vmin`, `vmax`, and the
         * two axes — and four in each of the three overridable ones. */
        ROBO_CHECK_EQ_INT(viewport, 18);
        ROBO_CHECK_EQ_INT(families[ROBO_CSS_VIEWPORT_DEFAULT], 6);
        ROBO_CHECK_EQ_INT(families[ROBO_CSS_VIEWPORT_SMALL], 4);
        ROBO_CHECK_EQ_INT(families[ROBO_CSS_VIEWPORT_LARGE], 4);
        ROBO_CHECK_EQ_INT(families[ROBO_CSS_VIEWPORT_DYNAMIC], 4);
    }
    ROBO_CHECK_EQ_INT(robo_css_unit_viewport_family(ROBO_CSS_UNIT_VW),
                      ROBO_CSS_VIEWPORT_DEFAULT);
    ROBO_CHECK_EQ_INT(robo_css_unit_viewport_family(ROBO_CSS_UNIT_SVH),
                      ROBO_CSS_VIEWPORT_SMALL);
    ROBO_CHECK_EQ_INT(robo_css_unit_viewport_family(ROBO_CSS_UNIT_LVMIN),
                      ROBO_CSS_VIEWPORT_LARGE);
    ROBO_CHECK_EQ_INT(robo_css_unit_viewport_family(ROBO_CSS_UNIT_DVW),
                      ROBO_CSS_VIEWPORT_DYNAMIC);
    /* Documented rather than incidental: the family question is only asked of a
     * viewport unit, and a non-viewport one answers with the family every such
     * unit would use. */
    ROBO_CHECK_EQ_INT(robo_css_unit_viewport_family(ROBO_CSS_UNIT_PX),
                      ROBO_CSS_VIEWPORT_DEFAULT);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_viewport(ROBO_CSS_UNIT_PX), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_viewport(ROBO_CSS_UNIT_NONE), 0);
}

/*
 * CSS's absolute relationships, exactly as the specification defines them.
 *
 * Every pair below is a *definition* — 1in is 96px, 2.54cm is 1in — so the
 * assertion is equality within a tolerance rather than an approximate match.
 * The tolerance is 1e-9 relative to the larger operand, because a chain of
 * multiplications by 96/2.54 cannot be bit-exact and pretending otherwise would
 * make this test fail on a different compiler rather than a different value.
 */
static void css_unit_assert_relationship(double value, robo_css_unit_t unit,
                                         double expected)
{
    double px = 0.0;
    double tolerance = (expected < 0.0 ? -expected : expected) * 1e-9;
    char message[128];

    ROBO_CHECK_EQ_INT((int)robo_css_unit_resolve_px(value, unit, NULL, &px),
                      (int)ROBO_CSS_LENGTH_RESOLVED);
    if (!css_unit_near(px, expected, tolerance < 1e-9 ? 1e-9 : tolerance)) {
        sprintf(message, "%.10g%s resolved to %.10g, expected %.10g", value,
                robo_css_unit_name(unit), px, expected);
        ROBO_CHECK_MSG(0, message);
    }
}

static void test_unit_absolute_conversion(void)
{
    double px = 0.0;

    css_fixture();

    /* The reference pixel itself, and the trivial case a converter is most
     * likely to get wrong by multiplying with a missing factor. */
    css_unit_assert_relationship(1.0, ROBO_CSS_UNIT_PX, 1.0);
    css_unit_assert_relationship(96.0, ROBO_CSS_UNIT_PX, 96.0);
    css_unit_assert_relationship(1.5, ROBO_CSS_UNIT_PX, 1.5);
    css_unit_assert_relationship(1.0, ROBO_CSS_UNIT_IN, 96.0);
    css_unit_assert_relationship(2.0, ROBO_CSS_UNIT_IN, 192.0);

    /* The definitional pairs, one per unit. */
    css_unit_assert_relationship(2.54, ROBO_CSS_UNIT_CM, 96.0);
    css_unit_assert_relationship(25.4, ROBO_CSS_UNIT_MM, 96.0);
    /* `Q` is a quarter of a millimetre, so forty of them are a centimetre and
     * four are a millimetre — asserted both ways round, because a factor that
     * was out by a factor of forty would pass a test that only checked one. */
    css_unit_assert_relationship(40.0, ROBO_CSS_UNIT_Q, 96.0 / 2.54);
    css_unit_assert_relationship(4.0, ROBO_CSS_UNIT_Q, 96.0 / 25.4);
    css_unit_assert_relationship(72.0, ROBO_CSS_UNIT_PT, 96.0);
    css_unit_assert_relationship(6.0, ROBO_CSS_UNIT_PC, 96.0);
    /* 12pt is 1pc, so the two must agree about sixteen pixels. */
    css_unit_assert_relationship(12.0, ROBO_CSS_UNIT_PT, 16.0);
    css_unit_assert_relationship(1.0, ROBO_CSS_UNIT_PC, 16.0);

    /* Zero and negative: a length is a signed quantity, and zero is the value
     * every unit agrees about. */
    css_unit_assert_relationship(0.0, ROBO_CSS_UNIT_IN, 0.0);
    css_unit_assert_relationship(0.0, ROBO_CSS_UNIT_CM, 0.0);
    css_unit_assert_relationship(-1.0, ROBO_CSS_UNIT_IN, -96.0);

    /* `to_px` is the *conversion* door rather than the resolution one, and it
     * answers with its input unchanged for a unit that has no fixed factor —
     * which is why it must never be asked whether a unit is absolute. */
    ROBO_CHECK(css_unit_near(robo_css_unit_to_px(2.0, ROBO_CSS_UNIT_EM), 2.0, 1e-12));
    ROBO_CHECK(css_unit_near(robo_css_unit_to_px(50.0, ROBO_CSS_UNIT_VH), 50.0, 1e-12));
    ROBO_CHECK(css_unit_near(robo_css_unit_to_px(7.0, ROBO_CSS_UNIT_NONE), 7.0, 1e-12));
    ROBO_CHECK(css_unit_near(robo_css_unit_to_px(7.0, (robo_css_unit_t)99), 7.0, 1e-12));

    /* An absolute length reads the same with no context and with an empty one:
     * that is the entire content of "a definition rather than a measurement". */
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_IN, NULL, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 96.0, 1e-9));
    {
        robo_css_unit_context_t context;

        robo_css_unit_context_init(&context);
        ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_IN, &context, &px),
                          ROBO_CSS_LENGTH_RESOLVED);
        ROBO_CHECK(css_unit_near(px, 96.0, 1e-9));
    }

    /* The resolution vocabulary is closed, and its names are the names a
     * developer reads in a diagnostic. */
    ROBO_CHECK_EQ_INT(robo_css_length_resolution_is_known(ROBO_CSS_LENGTH_RESOLVED), 1);
    ROBO_CHECK_EQ_INT(robo_css_length_resolution_is_known(ROBO_CSS_LENGTH_INVALID), 1);
    ROBO_CHECK_EQ_INT(robo_css_length_resolution_is_known((robo_css_length_resolution_t)9),
                      0);
    ROBO_CHECK_STR_EQ(robo_css_length_resolution_name(ROBO_CSS_LENGTH_RESOLVED),
                      "resolved");
    ROBO_CHECK_STR_EQ(robo_css_length_resolution_name(ROBO_CSS_LENGTH_NEEDS_CONTEXT),
                      "needs-context");
    ROBO_CHECK_STR_EQ(robo_css_length_resolution_name(ROBO_CSS_LENGTH_NOT_A_LENGTH),
                      "not-a-length");
    ROBO_CHECK_STR_EQ(robo_css_length_resolution_name(ROBO_CSS_LENGTH_INVALID), "invalid");
    ROBO_CHECK_STR_EQ(robo_css_length_resolution_name((robo_css_length_resolution_t)9),
                      "unknown");
}

/* ------------------------------------------------------------------ */
/* Contextual resolution                                               */
/* ------------------------------------------------------------------ */

/**
 * The seven absolute units, resolved rather than merely converted.
 *
 * The distinction between this and `robo_css_unit_to_px` is the point: the
 * resolver reports *whether* the unit is a length, and a value of zero is
 * RESOLVED rather than NOT_A_LENGTH. Asserted with a context that knows a font
 * size and a viewport, because an absolute unit must ignore both — a resolver
 * that consulted the context for `1in` would have answered a question about the
 * display that CSS's definition of `in` does not ask.
 */
static void test_unit_resolve_absolute(void)
{
    static const struct {
        robo_css_unit_t unit;
        double value;
        double px;
    } cases[] = {
        { ROBO_CSS_UNIT_PX, 0.0, 0.0 },
        { ROBO_CSS_UNIT_PX, 12.5, 12.5 },
        { ROBO_CSS_UNIT_IN, 1.0, 96.0 },
        { ROBO_CSS_UNIT_IN, -1.0, -96.0 },
        { ROBO_CSS_UNIT_CM, 1.0, 96.0 / 2.54 },
        { ROBO_CSS_UNIT_MM, 1.0, 96.0 / 25.4 },
        { ROBO_CSS_UNIT_Q, 1.0, 96.0 / 101.6 },
        { ROBO_CSS_UNIT_PT, 1.0, 1.3333333333333333 },
        { ROBO_CSS_UNIT_PC, 1.0, 16.0 }
    };
    robo_css_unit_context_t context;
    size_t index;
    double px = 0.0;
    int unit;

    css_fixture();
    robo_css_unit_context_init(&context);
    robo_css_unit_context_set_font(&context, 100.0, 50.0, 60.0, 150.0);
    robo_css_unit_context_set_viewport(&context, 1000.0, 500.0);

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        double tolerance = (cases[index].px < 0.0 ? -cases[index].px : cases[index].px) * 1e-9;

        ROBO_CHECK_EQ_INT((int)robo_css_unit_resolve_px(cases[index].value, cases[index].unit,
                                                       &context, &px),
                          (int)ROBO_CSS_LENGTH_RESOLVED);
        ROBO_CHECK(css_unit_near(px, cases[index].px, tolerance < 1e-9 ? 1e-9 : tolerance));
    }

    /* All seven are absolute, and the six that are not are not — asked of the
     * whole set rather than of a sample, because "some relative unit is really
     * absolute" is exactly the defect this phase's predecessor had. */
    for (unit = (int)ROBO_CSS_UNIT_NONE; unit < (int)ROBO_CSS_UNIT_COUNT; unit++) {
        robo_css_unit_class_t unit_class = robo_css_unit_class((robo_css_unit_t)unit);
        int expected = unit_class == ROBO_CSS_UNIT_CLASS_ABSOLUTE;

        ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute((robo_css_unit_t)unit), expected);
    }

    /* A contextual unit still refuses with a context that knows a font size and
     * a viewport, when the fact it needs is an axis or a family rather than a
     * size — and `50%` is not a unit at all. */
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_REM, &context, &px),
                      ROBO_CSS_LENGTH_NEEDS_CONTEXT);
}

/**
 * Every contextual unit answers NEEDS_CONTEXT with no context at all.
 *
 * This is the phase's central claim, so it is asserted for the whole set rather
 * than for `2em` and `50vh`: a resolver that converted *any* of these without a
 * context would have invented the font size or the viewport it needed.
 */
static void test_unit_resolve_needs_context(void)
{
    robo_css_unit_context_t empty;
    int unit;
    double px = 12345.0;

    css_fixture();
    robo_css_unit_context_init(&empty);

    for (unit = (int)ROBO_CSS_UNIT_NONE; unit < (int)ROBO_CSS_UNIT_COUNT; unit++) {
        robo_css_unit_t enumerator = (robo_css_unit_t)unit;

        if (enumerator == ROBO_CSS_UNIT_NONE || robo_css_unit_is_absolute(enumerator)) {
            continue;
        }
        /* NULL is a caller that knows nothing; an initialized context is a caller
         * that knows nothing and said so. Neither may produce a number. */
        ROBO_CHECK_EQ_INT((int)robo_css_unit_resolve_px(2.0, enumerator, NULL, &px),
                          (int)ROBO_CSS_LENGTH_NEEDS_CONTEXT);
        ROBO_CHECK_EQ_INT((int)robo_css_unit_resolve_px(2.0, enumerator, &empty, &px),
                          (int)ROBO_CSS_LENGTH_NEEDS_CONTEXT);
    }
}

static void test_unit_resolve_font_relative(void)
{
    robo_css_unit_context_t context;
    double px = 0.0;

    css_fixture();
    robo_css_unit_context_init(&context);

    /* The element's own metrics only: `rem` must still refuse, because a context
     * that knows the element's font does not know the root's. */
    robo_css_unit_context_set_font(&context, 16.0, 8.0, 9.0, 24.0);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(2.0, ROBO_CSS_UNIT_EM, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 32.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_EX, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 8.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(3.0, ROBO_CSS_UNIT_CH, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 27.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_LH, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 24.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_REM, &context, &px),
                      ROBO_CSS_LENGTH_NEEDS_CONTEXT);

    /* The root's metrics only: the mirror image, and the reason the two sets of
     * fields are separate rather than one "font context". */
    {
        robo_css_unit_context_t root;

        robo_css_unit_context_init(&root);
        robo_css_unit_context_set_root_font(&root, 20.0, 30.0);
        ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(2.0, ROBO_CSS_UNIT_REM, &root, &px),
                          ROBO_CSS_LENGTH_RESOLVED);
        ROBO_CHECK(css_unit_near(px, 40.0, 1e-9));
        ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_RLH, &root, &px),
                          ROBO_CSS_LENGTH_RESOLVED);
        ROBO_CHECK(css_unit_near(px, 30.0, 1e-9));
        ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_EM, &root, &px),
                          ROBO_CSS_LENGTH_NEEDS_CONTEXT);
    }

    /* Both, and the values are the ones supplied rather than a default: a
     * resolver that used 16 for everything would pass the two tests above by
     * coincidence and fail these. */
    robo_css_unit_context_set_root_font(&context, 10.0, 15.0);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.5, ROBO_CSS_UNIT_REM, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 15.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(0.5, ROBO_CSS_UNIT_EM, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 8.0, 1e-9));
    /* Zero is zero whatever the context says, and zero with no context is still
     * *unresolvable* rather than zero — the context is what is missing, not the
     * magnitude. */
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(0.0, ROBO_CSS_UNIT_EM, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 0.0, 1e-12));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(0.0, ROBO_CSS_UNIT_EM, NULL, &px),
                      ROBO_CSS_LENGTH_NEEDS_CONTEXT);
}

static void test_unit_resolve_viewport(void)
{
    robo_css_unit_context_t context;
    double px = 0.0;

    css_fixture();
    robo_css_unit_context_init(&context);
    robo_css_unit_context_set_viewport(&context, 1000.0, 500.0);

    /* One percent per unit, so the expected value is the dimension over a
     * hundred — and vmin/vmax pick the dimension rather than the value. */
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(50.0, ROBO_CSS_UNIT_VW, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 500.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(10.0, ROBO_CSS_UNIT_VH, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 50.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(10.0, ROBO_CSS_UNIT_VMIN, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 50.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(10.0, ROBO_CSS_UNIT_VMAX, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 100.0, 1e-9));

    /* The axes default to the horizontal writing mode, so `vi` is the width and
     * `vb` the height — and swapping the flag swaps them. */
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_VI, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 1000.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_VB, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 500.0, 1e-9));
    context.vertical_writing = 1;
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_VI, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 500.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_VB, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 1000.0, 1e-9));
    context.vertical_writing = 0;

    /* The three families fall back to the default, which is what makes a window
     * whose chrome never moves expressible once rather than four times. Every
     * `sv*`/`lv*`/`dv*` unit must agree with its default-family relative. */
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(50.0, ROBO_CSS_UNIT_SVW, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 500.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(50.0, ROBO_CSS_UNIT_LVW, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 500.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(50.0, ROBO_CSS_UNIT_DVW, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 500.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_SVMIN, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 500.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_LVMAX, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 1000.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(10.0, ROBO_CSS_UNIT_DVH, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 50.0, 1e-9));

    /* With the families *set*, each answers its own size instead — and the
     * default family keeps answering the default one, which is the boundary
     * between the fallback chain and a plain override. */
    robo_css_unit_context_set_small_viewport(&context, 800.0, 400.0);
    robo_css_unit_context_set_large_viewport(&context, 1000.0, 600.0);
    robo_css_unit_context_set_dynamic_viewport(&context, 900.0, 450.0);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_SVW, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 800.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_LVH, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 600.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_DVW, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 900.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_VW, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 1000.0, 1e-9));

    /* A viewport unit that is not a percentage of the *other* dimension: `vh`
     * does not read the width, which a resolver over a single "viewport size"
     * would get wrong. */
    robo_css_unit_context_init(&context);
    robo_css_unit_context_set_viewport(&context, 300.0, 700.0);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_VMIN, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 300.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(100.0, ROBO_CSS_UNIT_VMAX, &context, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 700.0, 1e-9));
}

static void test_unit_resolve_invalid(void)
{
    robo_css_unit_context_t context;
    double px = 12345.0;
    double overflow = css_unit_overflow();
    double not_a_number = overflow - overflow;

    css_fixture();
    robo_css_unit_context_init(&context);

    /* The overflow multiplier actually overflowed, so the test below is about
     * the module rather than about this helper. */
    ROBO_CHECK_EQ_INT(overflow == overflow, 1);
    ROBO_CHECK_EQ_INT((overflow - overflow) == 0.0, 0);
    ROBO_CHECK_EQ_INT(not_a_number == not_a_number, 0);

    /* Not a unit at all: NONE is the absence of a unit, and an unknown
     * enumerator is not one either. Distinguished from INVALID, because "this is
     * not a length" and "this length is not a number" are different fixes. */
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_NONE, NULL, &px),
                      ROBO_CSS_LENGTH_NOT_A_LENGTH);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, (robo_css_unit_t)99, NULL, &px),
                      ROBO_CSS_LENGTH_NOT_A_LENGTH);

    /* A magnitude that is not finite, for a unit that would otherwise resolve.
     * Asserted for an absolute unit too: the guard is about the *value*, and an
     * absolute unit is the case where nothing else could refuse it. */
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(not_a_number, ROBO_CSS_UNIT_PX, NULL, &px),
                      ROBO_CSS_LENGTH_INVALID);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(overflow, ROBO_CSS_UNIT_PX, NULL, &px),
                      ROBO_CSS_LENGTH_INVALID);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(not_a_number, ROBO_CSS_UNIT_EM, &context, &px),
                      ROBO_CSS_LENGTH_INVALID);

    /* A context input that is not finite is INVALID rather than NEEDS_CONTEXT:
     * the caller did supply a font size and it is not one, which is fixable
     * information rather than "ask again later". */
    robo_css_unit_context_set_font(&context, not_a_number, 8.0, 9.0, 24.0);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_EM, &context, &px),
                      ROBO_CSS_LENGTH_INVALID);
    robo_css_unit_context_init(&context);
    robo_css_unit_context_set_viewport(&context, overflow, 500.0);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(50.0, ROBO_CSS_UNIT_VW, &context, &px),
                      ROBO_CSS_LENGTH_INVALID);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(50.0, ROBO_CSS_UNIT_SVW, &context, &px),
                      ROBO_CSS_LENGTH_INVALID);

    /* The multiplication's own overflow: `1e308in` is a finite number and an
     * infinite length, and an infinity that reached a layout would be worse than
     * a refusal. `1e300in` is *not* an example — 9.6e301 is finite — which is
     * what makes the magnitude here worth choosing rather than guessing. */
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1e308, ROBO_CSS_UNIT_IN, NULL, &px),
                      ROBO_CSS_LENGTH_INVALID);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1e300, ROBO_CSS_UNIT_IN, NULL, &px),
                      ROBO_CSS_LENGTH_RESOLVED);

    /* Nothing was written for any refusal: the out-parameter is only ever the
     * caller's to read after RESOLVED, and this is what makes that true. */
    px = 12345.0;
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(2.0, ROBO_CSS_UNIT_EM, NULL, &px),
                      ROBO_CSS_LENGTH_NEEDS_CONTEXT);
    ROBO_CHECK(css_unit_near(px, 12345.0, 1e-9));
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_NONE, NULL, &px),
                      ROBO_CSS_LENGTH_NOT_A_LENGTH);
    ROBO_CHECK(css_unit_near(px, 12345.0, 1e-9));

    /* A NULL output is a caller that only wants the answer, which is legal. */
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_IN, NULL, NULL),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_EM, NULL, NULL),
                      ROBO_CSS_LENGTH_NEEDS_CONTEXT);
}

static void test_unit_context(void)
{
    robo_css_unit_context_t context;

    css_fixture();

    /* A cleared context knows nothing, which is the state every flag exists to
     * express — and the state a factory-allocated struct would start in by luck
     * rather than by contract. */
    memset(&context, 0xFF, sizeof(context));
    robo_css_unit_context_init(&context);
    ROBO_CHECK_EQ_INT(context.has_font_metrics, 0);
    ROBO_CHECK_EQ_INT(context.has_root_font_metrics, 0);
    ROBO_CHECK_EQ_INT(context.has_viewport, 0);
    ROBO_CHECK_EQ_INT(context.has_small_viewport, 0);
    ROBO_CHECK_EQ_INT(context.has_large_viewport, 0);
    ROBO_CHECK_EQ_INT(context.has_dynamic_viewport, 0);
    ROBO_CHECK_EQ_INT(context.vertical_writing, 0);

    robo_css_unit_context_set_font(&context, 16.0, 8.0, 9.0, 24.0);
    ROBO_CHECK_EQ_INT(context.has_font_metrics, 1);
    ROBO_CHECK(css_unit_near(context.font_size, 16.0, 1e-12));
    ROBO_CHECK(css_unit_near(context.x_height, 8.0, 1e-12));
    ROBO_CHECK(css_unit_near(context.zero_width, 9.0, 1e-12));
    ROBO_CHECK(css_unit_near(context.line_height, 24.0, 1e-12));
    ROBO_CHECK_EQ_INT(context.has_root_font_metrics, 0);

    robo_css_unit_context_set_root_font(&context, 20.0, 30.0);
    ROBO_CHECK_EQ_INT(context.has_root_font_metrics, 1);
    robo_css_unit_context_set_viewport(&context, 1000.0, 500.0);
    robo_css_unit_context_set_small_viewport(&context, 800.0, 400.0);
    robo_css_unit_context_set_large_viewport(&context, 1000.0, 600.0);
    robo_css_unit_context_set_dynamic_viewport(&context, 900.0, 450.0);
    ROBO_CHECK_EQ_INT(context.has_viewport, 1);
    ROBO_CHECK_EQ_INT(context.has_small_viewport, 1);
    ROBO_CHECK_EQ_INT(context.has_large_viewport, 1);
    ROBO_CHECK_EQ_INT(context.has_dynamic_viewport, 1);

    /* Re-initializing clears what a setter set, so "I know nothing" is reachable
     * after "I knew something" — the state a caller reusing a context needs. */
    robo_css_unit_context_init(&context);
    ROBO_CHECK_EQ_INT(context.has_font_metrics, 0);
    ROBO_CHECK_EQ_INT(context.has_viewport, 0);

    /* Every setter tolerates NULL, because a caller that has no context to fill
     * has nothing to do rather than a bug. */
    robo_css_unit_context_init(NULL);
    robo_css_unit_context_set_font(NULL, 1.0, 1.0, 1.0, 1.0);
    robo_css_unit_context_set_root_font(NULL, 1.0, 1.0);
    robo_css_unit_context_set_viewport(NULL, 1.0, 1.0);
    robo_css_unit_context_set_small_viewport(NULL, 1.0, 1.0);
    robo_css_unit_context_set_large_viewport(NULL, 1.0, 1.0);
    robo_css_unit_context_set_dynamic_viewport(NULL, 1.0, 1.0);
    ROBO_CHECK(1);
}

/* ------------------------------------------------------------------ */
/* The typed numeric value                                             */
/* ------------------------------------------------------------------ */

static void test_numeric_kinds(void)
{
    int kind;

    css_fixture();

    for (kind = 0; kind < (int)ROBO_CSS_NUMERIC_KIND_COUNT; kind++) {
        ROBO_CHECK_EQ_INT(robo_css_numeric_kind_is_known((robo_css_numeric_kind_t)kind), 1);
    }
    ROBO_CHECK_EQ_INT(robo_css_numeric_kind_is_known((robo_css_numeric_kind_t)99), 0);
    ROBO_CHECK_STR_EQ(robo_css_numeric_kind_name(ROBO_CSS_NUMERIC_NONE), "none");
    ROBO_CHECK_STR_EQ(robo_css_numeric_kind_name(ROBO_CSS_NUMERIC_NUMBER), "number");
    ROBO_CHECK_STR_EQ(robo_css_numeric_kind_name(ROBO_CSS_NUMERIC_INTEGER), "integer");
    ROBO_CHECK_STR_EQ(robo_css_numeric_kind_name(ROBO_CSS_NUMERIC_PERCENTAGE),
                      "percentage");
    ROBO_CHECK_STR_EQ(robo_css_numeric_kind_name(ROBO_CSS_NUMERIC_LENGTH), "length");
    ROBO_CHECK_STR_EQ(robo_css_numeric_kind_name((robo_css_numeric_kind_t)99), "none");

    /* A cleared record is not a value, and its fields are the ones the factory
     * writes rather than whatever was on the stack. */
    {
        robo_css_numeric_t none = robo_css_numeric_none();

        ROBO_CHECK_EQ_INT(none.kind, ROBO_CSS_NUMERIC_NONE);
        ROBO_CHECK(css_unit_near(none.number, 0.0, 1e-12));
        ROBO_CHECK_EQ_INT(none.unit, ROBO_CSS_UNIT_NONE);
        ROBO_CHECK_EQ_INT(robo_css_numeric_is_present(none), 0);
    }

    /* An integer is what was *written* as one, which is the grammar's question
     * and not the value's: `1.0` is whole and is not an integer. */
    css_numeric_is("n: 10", "10");
    ROBO_CHECK_STR_EQ(css_numeric_kind_of("n: 10"), "integer");
    css_numeric_is("n: -3", "-3");
    ROBO_CHECK_STR_EQ(css_numeric_kind_of("n: -3"), "integer");
    css_numeric_is("n: +7", "7");
    ROBO_CHECK_STR_EQ(css_numeric_kind_of("n: +7"), "integer");
    css_numeric_is("n: 0", "0");
    css_numeric_is("n: 1.0", "1");
    ROBO_CHECK_STR_EQ(css_numeric_kind_of("n: 1.0"), "number");
    css_numeric_is("n: 1.5", "1.5");
    ROBO_CHECK_STR_EQ(css_numeric_kind_of("n: 1.5"), "number");
    css_numeric_is("n: -0.25", "-0.25");
    css_numeric_is("n: .5", "0.5");
    ROBO_CHECK_STR_EQ(css_numeric_kind_of("n: .5"), "number");
    css_numeric_is("n: 1e2", "100");
    /* An exponent makes it a *number* even though the value is whole, which is
     * exactly the case a single "numeric" kind would erase. */
    ROBO_CHECK_STR_EQ(css_numeric_kind_of("n: 1e2"), "number");
    css_numeric_is("n: 1.25e-1", "0.125");
    css_numeric_is("n: 1e+2", "100");

    /* Decimal places survive, and trailing zeroes do not survive as text. */
    css_numeric_is("n: 1.500", "1.5");
    css_numeric_is("n: 0.000001", "0.000001");
    css_numeric_is("n: 123456.75", "123456.75");

    /* A percentage is a kind of its own and never a number, because 50% of a
     * width and 50 of a z-index are different computations. */
    css_numeric_is("n: 0%", "0%");
    css_numeric_is("n: 50%", "50%");
    css_numeric_is("n: 100%", "100%");
    css_numeric_is("n: 12.5%", "12.5%");
    css_numeric_is("n: -10%", "-10%");
    css_numeric_is("n: +25%", "25%");
    ROBO_CHECK_STR_EQ(css_numeric_kind_of("n: 50%"), "percentage");

    /* A dimension keeps its unit and its magnitude, and the unit is normalized
     * to the table's canonical spelling rather than the author's. */
    css_numeric_is("n: 10px", "10px");
    ROBO_CHECK_STR_EQ(css_numeric_kind_of("n: 10px"), "length");
    css_numeric_is("n: 10PX", "10px");
    css_numeric_is("n: 1.5em", "1.5em");
    css_numeric_is("n: 0.25rem", "0.25rem");
    css_numeric_is("n: -20vh", "-20vh");
    css_numeric_is("n: 3ch", "3ch");
    css_numeric_is("n: 4q", "4q");

    /* The four kinds are four, and a value that is none of them is not a numeric
     * value at all rather than a fifth kind. */
    {
        static const char *const kinds[] = { "none", "number", "integer",
                                            "percentage", "length" };
        size_t index;

        for (index = 0u; index < sizeof(kinds) / sizeof(kinds[0]); index++) {
            ROBO_CHECK_STR_EQ(robo_css_numeric_kind_name((robo_css_numeric_kind_t)index),
                              kinds[index]);
        }
    }
}

static void test_numeric_equality(void)
{
    robo_css_numeric_t ten = robo_css_numeric_none();
    robo_css_numeric_t ten_again = robo_css_numeric_none();
    robo_css_numeric_t ten_em = robo_css_numeric_none();
    robo_css_numeric_t fifty_percent = robo_css_numeric_none();
    robo_css_numeric_t fifty = robo_css_numeric_none();

    css_fixture();

    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&ten, 10.0, ROBO_CSS_UNIT_PX, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&ten_again, 10.0, ROBO_CSS_UNIT_PX,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&ten_em, 10.0, ROBO_CSS_UNIT_EM, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_percentage(&fifty_percent, 50.0, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&fifty, 50.0, 0, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_numeric_equals(ten, ten_again), 1);
    /* The unit is part of the value: converting is the resolver's job, and a
     * comparison that converted would answer a question about a context the
     * comparison does not have. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_equals(ten, ten_em), 0);
    /* The kind is part of the value too, and this is the pair that matters: a
     * length used where a proportion was meant is a layout bug that a
     * kind-blind comparison would make invisible. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_equals(fifty_percent, fifty), 0);

    /* A cleared record equals another cleared record, and nothing else. */
    {
        robo_css_numeric_t none = robo_css_numeric_none();
        robo_css_numeric_t zero = robo_css_numeric_none();

        ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&zero, 0.0, 1, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_numeric_equals(none, none), 1);
        ROBO_CHECK_EQ_INT(robo_css_numeric_equals(none, zero), 0);
    }

    /* Integrality is a question about the value; integer-ness is a question
     * about the grammar. They answer differently for `1.0`, which is the whole
     * point of having both. */
    {
        robo_css_numeric_t value = robo_css_numeric_none();
        int64_t integer = 0;

        ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, 10.0, 0, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_numeric_is_integral(value), 1);
        /* A NUMBER is refused even when whole, because a property whose grammar
         * says `<integer>` must refuse `1.0`. */
        ROBO_CHECK_EQ_INT(robo_css_numeric_integer_value(value, &integer), 0);

        ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, 10.0, 1, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_numeric_integer_value(value, &integer), 1);
        ROBO_CHECK_EQ_INT(integer, 10);

        ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, 10.5, 0, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_numeric_is_integral(value), 0);
        ROBO_CHECK_EQ_INT(robo_css_numeric_integer_value(value, &integer), 0);

        /* Beyond 2^53 a double cannot represent every integer, so "is this
         * whole" has no reliable answer — and the answer given is no rather than
         * a cast that would be undefined behaviour. */
        ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, 1e300, 1, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_numeric_is_integral(value), 0);
        ROBO_CHECK_EQ_INT(robo_css_numeric_integer_value(value, &integer), 0);
        ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, 9007199254740992.0, 1, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_numeric_is_integral(value), 1);
        ROBO_CHECK_EQ_INT(robo_css_numeric_integer_value(value, &integer), 1);
        ROBO_CHECK_EQ_INT(integer, 9007199254740992LL);

        /* Negative values convert, and the boundary is symmetric. */
        ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, -12.0, 1, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_numeric_integer_value(value, &integer), 1);
        ROBO_CHECK_EQ_INT(integer, -12);
        ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, -1e300, 1, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_numeric_integer_value(value, &integer), 0);

        /* A NULL output is a caller asking whether, not what. */
        ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, 4.0, 1, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_numeric_integer_value(value, NULL), 1);
    }
}

static void test_numeric_constructors(void)
{
    robo_css_numeric_t value = robo_css_numeric_none();
    double overflow = css_unit_overflow();

    css_fixture();

    /* Every constructor refuses a magnitude that is not finite, and refuses it as
     * a programmer error rather than a parse outcome: the tokenizer cannot
     * produce one of these without the parser having rejected it first. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, overflow, 0, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_percentage(&value, overflow, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&value, overflow, ROBO_CSS_UNIT_PX, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        double not_a_number = overflow - overflow;

        ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, not_a_number, 0, NULL),
                          ROBO_INVALID_ARGUMENT);
    }

    /* A length with no unit is a number, so it is refused: the caller who meant
     * a number has a constructor for that. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&value, 1.0, ROBO_CSS_UNIT_NONE, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&value, 1.0, (robo_css_unit_t)99, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&value, 1.0, ROBO_CSS_UNIT_PX, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&value, 1.0, ROBO_CSS_UNIT_DVMAX, NULL),
                      ROBO_OK);

    /* A NULL output is a bug rather than a value. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(NULL, 1.0, 0, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_percentage(NULL, 1.0, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(NULL, 1.0, ROBO_CSS_UNIT_PX, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A failing constructor leaves its output alone rather than half-written:
     * `robo_css_numeric_none` is not stamped on a value the caller may still be
     * reading. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&value, 7.0, ROBO_CSS_UNIT_VW, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&value, overflow, 0, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(value.kind, ROBO_CSS_NUMERIC_LENGTH);
    ROBO_CHECK_EQ_INT(value.unit, ROBO_CSS_UNIT_VW);
}

/* ------------------------------------------------------------------ */
/* Parse outcomes                                                      */
/* ------------------------------------------------------------------ */

/**
 * The five outcomes, one input each.
 *
 * They stay five because they tell a developer five things, and the pairs that
 * are easiest to confuse are the ones asserted most carefully: `10foo` is a
 * dimension whose *unit* is unknown, `red blue` is not numeric at all, and
 * `calc(...)` is legal CSS this build does not evaluate.
 */
static void test_numeric_parse_outcomes(void)
{
    robo_css_numeric_t numeric = robo_css_numeric_none();
    robo_css_numeric_t parsed = robo_css_numeric_none();
    int outcome;

    css_fixture();

    for (outcome = 0; outcome < (int)ROBO_CSS_NUMERIC_PARSE_COUNT; outcome++) {
        ROBO_CHECK_EQ_INT(robo_css_numeric_parse_is_known((robo_css_numeric_parse_t)outcome),
                          1);
    }
    ROBO_CHECK_EQ_INT(robo_css_numeric_parse_is_known((robo_css_numeric_parse_t)99), 0);
    ROBO_CHECK_STR_EQ(robo_css_numeric_parse_name(ROBO_CSS_NUMERIC_PARSE_INVALID),
                      "invalid");
    ROBO_CHECK_STR_EQ(robo_css_numeric_parse_name(ROBO_CSS_NUMERIC_PARSE_RESOLVED),
                      "resolved");
    ROBO_CHECK_STR_EQ(robo_css_numeric_parse_name(ROBO_CSS_NUMERIC_PARSE_DEFERRED),
                      "deferred");
    ROBO_CHECK_STR_EQ(robo_css_numeric_parse_name(ROBO_CSS_NUMERIC_PARSE_UNKNOWN_UNIT),
                      "unknown-unit");
    ROBO_CHECK_STR_EQ(robo_css_numeric_parse_name(ROBO_CSS_NUMERIC_PARSE_NON_FINITE),
                      "non-finite");
    ROBO_CHECK_STR_EQ(robo_css_numeric_parse_name((robo_css_numeric_parse_t)99),
                      "invalid");

    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: 10px"), "resolved");
    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: 10foo"), "unknown-unit");
    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: 1e999"), "non-finite");
    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: -1e999"), "non-finite");

    /* Every reference function, because they are one group rather than a
     * special case for `calc()`: each is legal CSS whose value is not a number
     * yet, and each must be carried rather than called a mistake. */
    {
        static const char *const deferred[] = {
            "calc(1px + 2px)", "var(--x)",           "min(1px, 2px)",
            "max(1px, 2px)",   "clamp(1px, 2px, 3px)", "env(safe-area-inset-top)"
        };
        size_t index;

        for (index = 0u; index < sizeof(deferred) / sizeof(deferred[0]); index++) {
            char declaration[64];

            sprintf(declaration, "n: %s", deferred[index]);
            ROBO_CHECK_STR_EQ(css_numeric_outcome_of(declaration), "deferred");
        }
    }
    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: red"), "invalid");
    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: rgb(1, 2, 3)"), "invalid");
    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: url(a.png)"), "invalid");
    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: \"10px\""), "invalid");
    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: 10px 20px"), "invalid");
    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: 10 20"), "invalid");
    ROBO_CHECK_STR_EQ(css_numeric_outcome_of("n: 10 px"), "invalid");

    /* A refused parse leaves the caller's record untouched, which is what makes
     * "check the outcome, then read the value" a safe order. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&numeric, 7.0, ROBO_CSS_UNIT_VH, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)css_numeric_parse("n: 10foo", &parsed),
                      (int)ROBO_CSS_NUMERIC_PARSE_UNKNOWN_UNIT);
    ROBO_CHECK_EQ_INT(robo_css_numeric_equals(numeric,
                                              robo_css_numeric_none()),
                      0);

    /*
     * The value door and the token door agree; the token door is simply asked of
     * a token rather than of a tree.
     *
     * The component is reached by walking the public child list rather than
     * through the internal "significant component" helper, so this suite sees
     * exactly what a caller outside RoboCSS's implementation sees.
     */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        const robo_css_value_t *value = css_unit_value_of("n: 2.5rem", &list);
        const robo_css_value_t *single = NULL;
        size_t child;

        for (child = 0u; child < robo_css_value_child_count(value); child++) {
            const robo_css_value_t *candidate = robo_css_value_child(value, child);

            if (candidate && candidate->kind == ROBO_CSS_VALUE_KIND_TOKEN) {
                single = candidate;
            }
        }
        ROBO_CHECK(single != NULL);
        ROBO_CHECK_EQ_INT((int)robo_css_numeric_from_token(&single->token, &parsed),
                          (int)ROBO_CSS_NUMERIC_PARSE_RESOLVED);
        ROBO_CHECK_EQ_INT(parsed.kind, ROBO_CSS_NUMERIC_LENGTH);
        ROBO_CHECK_EQ_INT(parsed.unit, ROBO_CSS_UNIT_REM);
        ROBO_CHECK(css_unit_near(parsed.number, 2.5, 1e-12));
        robo_css_declaration_list_dispose(&list);
    }

    /* NULL is a caller error rather than an outcome, and answers INVALID rather
     * than dereferencing. */
    ROBO_CHECK_EQ_INT((int)robo_css_numeric_from_value(NULL, &parsed),
                      (int)ROBO_CSS_NUMERIC_PARSE_INVALID);
    ROBO_CHECK_EQ_INT((int)robo_css_numeric_from_token(NULL, &parsed),
                      (int)ROBO_CSS_NUMERIC_PARSE_INVALID);
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT((int)robo_css_numeric_from_value(css_unit_value_of("n: 1px", &list),
                                                          NULL),
                          (int)ROBO_CSS_NUMERIC_PARSE_INVALID);
        robo_css_declaration_list_dispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* The canonical spelling                                              */
/* ------------------------------------------------------------------ */

/**
 * The writer, and the four rules a reader can check by eye.
 *
 * The one that is a decision rather than a convention is the last: a magnitude
 * below the fixed-point precision is written in a compact exponent form rather
 * than as `0`, because printing a value that is not zero as zero is a lie a
 * layout would then act on.
 */
static void test_numeric_write(void)
{
    robo_css_numeric_t numeric = robo_css_numeric_none();
    char buffer[ROBO_CSS_NUMERIC_TEXT_MAX];
    char small[8];
    size_t length = 0u;
    size_t needed = 0u;

    css_fixture();

    /* An integral magnitude has no decimal point, a fractional one has no
     * trailing zeroes, and a negative zero is zero. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&numeric, 10.0, 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "10");
    ROBO_CHECK_EQ_INT(length, 2u);

    ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&numeric, 1.5, 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "1.5");

    ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&numeric, 1.0, 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      ROBO_OK);
    /* `1.0` is whole, so it prints as `1` — while its *kind* stays NUMBER, which
     * is the distinction the spelling must not be asked to carry. */
    ROBO_CHECK_STR_EQ(buffer, "1");

    ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&numeric, -0.0, 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "0");

    /* A percentage carries its sign and a length its canonical unit spelling,
     * which is also what proves the unit's case-insensitivity end to end. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_percentage(&numeric, -12.5, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "-12.5%");
    ROBO_CHECK_EQ_INT(length, 6u);

    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&numeric, 20.0, ROBO_CSS_UNIT_VMIN, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "20vmin");

    /* The tiny-magnitude rule. `1e-9` rounded to six decimals is zero, and the
     * writer must not say so. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_number(&numeric, 1e-9, 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      ROBO_OK);
    ROBO_CHECK(buffer[0] != '\0');
    ROBO_CHECK(strcmp(buffer, "0") != 0);
    /* ...and it is the compact exponent form, which is the only spelling that can
     * carry a magnitude this small in a fixed-width buffer. */
    ROBO_CHECK(strchr(buffer, 'e') != NULL);

    /* A value that does not fit is refused rather than truncated, and the caller
     * learns how much room it needed. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&numeric, 123456.789, ROBO_CSS_UNIT_VMIN,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, small, sizeof(small), &needed, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_STR_EQ(small, "");
    ROBO_CHECK(needed > sizeof(small));

    /* The absence of a value has no text, and that is a success: a cleared
     * record is not a value that failed to write. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(robo_css_numeric_none(), buffer,
                                             sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "");
    ROBO_CHECK_EQ_INT(length, 0u);

    /* A NULL argument is a bug and not an outcome. */
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, NULL, sizeof(buffer), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, buffer, sizeof(buffer), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_numeric_write(numeric, buffer, 0u, &length, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/**
 * Malformed and extreme input, none of which may crash or invent a value.
 *
 * The shapes are chosen at the edges a numeric parser actually has: a unit that
 * is a prefix of another, an exponent with nothing after it, a sign with nothing
 * after it, a number whose magnitude does not fit a double, and a few strings
 * that are not numbers at all. Every one must come back as one of the five
 * outcomes rather than as a stack trace.
 */
static void test_numeric_robust(void)
{
    static const char *const declarations[] = {
        "n: 1.2.3",      "n: 1.",           "n: .",            "n: +",
        "n: -",          "n: e5",           "n: 1e",           "n: 1e+",
        "n: 1e999",      "n: -1e999",       "n: 1e-999",       "n: 999999999999999999999999",
        "n: 1pxfoo",     "n: px",           "n: 1p",           "n: 1pxx",
        "n: 1%%",        "n: %",            "n: 1 %",          "n: --x",
        "n: var(--a)",   "n: calc(",        "n: calc()",       "n: rgb(1, 2, 3)",
        "n: #fff",       "n: []",           "n: (1)",          "n: 1px 2px 3px",
        "n: 0x10",       "n: 1_000",        "n: 1,5",          "n: 1 5"
    };
    size_t index;

    css_fixture();

    for (index = 0u; index < sizeof(declarations) / sizeof(declarations[0]); index++) {
        robo_css_numeric_t numeric = robo_css_numeric_none();
        robo_css_numeric_parse_t outcome = css_numeric_parse(declarations[index], &numeric);
        char message[192];

        if (!robo_css_numeric_parse_is_known(outcome)) {
            sprintf(message, "%s produced an undefined parse outcome", declarations[index]);
            ROBO_CHECK_MSG(0, message);
            continue;
        }
        if (outcome == ROBO_CSS_NUMERIC_PARSE_RESOLVED) {
            /* A resolved value must be one of the four kinds and must have a
             * magnitude a runtime can hold: a parser that accepted `1.2.3` as
             * `1.2` would be the failure this test exists to catch. The
             * finiteness test is spelled out rather than taken from the module,
             * so the assertion is not the code under test. */
            int finite = numeric.number == numeric.number &&
                         (numeric.number - numeric.number) == 0.0;

            if (numeric.kind == ROBO_CSS_NUMERIC_NONE || !finite) {
                sprintf(message, "%s resolved to a value that is not one",
                        declarations[index]);
                ROBO_CHECK_MSG(0, message);
            }
        }
    }

    /* The same shapes through a whole declaration list, so the guard is about the
     * real parse path and not only about one declaration at a time. */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;
        size_t count;

        css_fixture();
        ROBO_CHECK_EQ_INT(css_parse_declarations(
                              "a: 1px; b: 1.2.3; c: 1e999; d: 10foo; e: calc(1px);"
                              "f: 50%; g: 2em; h: rgb(1,2,3)",
                              &list, &error),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_css_declaration_list_count(&list), 8);
        for (count = 0u; count < robo_css_declaration_list_count(&list); count++) {
            const robo_css_declaration_t *declaration = robo_css_declaration_list_at(&list,
                                                                                     count);
            robo_css_numeric_t numeric = robo_css_numeric_none();

            ROBO_CHECK(declaration != NULL);
            ROBO_CHECK(robo_css_numeric_parse_is_known(robo_css_numeric_from_value(
                robo_css_declaration_value(declaration), &numeric)));
        }
        robo_css_declaration_list_dispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* Integration: the computed-value layer                               */
/* ------------------------------------------------------------------ */

/**
 * Compute @p ref's style into @p out, asserting the walk itself was clean.
 *
 * A helper rather than the context call spelled out at each site, for the same
 * reason the colour suite has one: every expectation below is an expectation
 * about a document whose resolution *succeeded*, and an assertion that quietly
 * ran against a half-filled style would be an assertion about nothing.
 */
static void css_unit_compute_of(css_inherit_fixture_t *fixture,
                                robo_css_element_ref_t ref,
                                robo_css_computed_style_t *out)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(css_inherit_context(fixture), ref,
                                                          out, &error, NULL),
                      ROBO_OK);
    /* A unit this suite writes is legal, so a clean document is what every
     * expectation below is an expectation about. */
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
}

/**
 * A length-shaped property's computed values, across every unit family.
 *
 * The absolute half is converted *exactly* — `1in` and `96px` are one length, so
 * they must compute to one text — and the contextual half is carried with its unit
 * and its number. That split is the phase's contract with 9.8, and the two
 * assertions that pin it are `10em` and `50vh`: either converting them here or
 * dropping their units would be a number that looks computed and is wrong.
 */
static void test_unit_computed_lengths(void)
{
    css_fixture();

    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 10px", "10px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 1in", "96px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 2.54cm", "96px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 25.4mm", "96px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 72pt", "96px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 6pc", "96px");
    /* Forty `Q` are a centimetre, and 1cm is 96/2.54px — the same answer the
     * centimetre row above gives, because it is the same length. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 40q", "37.795276px");

    /* The units 9.10 added, carried rather than converted — including the three
     * viewport families and the two axes. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 1lh", "1lh");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 2rlh", "2rlh");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 3vi", "3vi");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 4vb", "4vb");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 50svw", "50svw");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 50svh", "50svh");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 50lvmin", "50lvmin");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 50dvmax", "50dvmax");

    /* The unit is *canonicalized*, not echoed: `10PX` is the same length as
     * `10px`, and the value layer writes one of them. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 10PX", "10px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 2EM", "2em");

    /* The kind stays a length and the unit stays live, which is what a later
     * phase resolves — so the assertions are on the typed record and not only on
     * the text. */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_computed_value_t computed = ROBO_CSS_CLEARED;
        const robo_css_declaration_t *declaration;

        robo_css_error_t error = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(css_parse_declarations("l: 2em", &list, &error), ROBO_OK);
        declaration = robo_css_declaration_list_at(&list, 0u);
        ROBO_CHECK(declaration != NULL);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                              robo_css_declaration_value(declaration), &computed, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(computed.kind, ROBO_CSS_COMPUTED_VALUE_LENGTH);
        ROBO_CHECK_EQ_INT(computed.unit, ROBO_CSS_UNIT_EM);
        ROBO_CHECK(css_unit_near(computed.number, 2.0, 1e-12));
        robo_css_computed_value_dispose(&computed);
        robo_css_declaration_list_dispose(&list);
    }
}

/**
 * Numbers and units through a real element, with inheritance and the CSS-wide
 * keywords.
 *
 * `font-size: 2em` on the child is the interesting one: the value stays relative,
 * because `em` on `font-size` is relative to the *parent's* font size and this
 * build has no font metrics to resolve it against. Carrying it is the honest
 * answer, and the test asserts the honesty rather than a number.
 */
static void test_unit_computed_inheritance(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *outer = NULL;
    robo_css_computed_style_t *relative = NULL;
    robo_css_computed_style_t *absolute = NULL;
    robo_css_computed_style_t *inherit_len = NULL;
    robo_css_computed_style_t *initial_len = NULL;
    robo_css_computed_style_t *unset_len = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t em_child;
    robo_css_element_ref_t in_child;
    robo_css_element_ref_t inherit_child;
    robo_css_element_ref_t initial_child;
    robo_css_element_ref_t unset_child;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "app", NULL);
    em_child = css_host_add(css_inherit_host(&fixture), "p", "a", NULL);
    in_child = css_host_add(css_inherit_host(&fixture), "p", "b", NULL);
    inherit_child = css_host_add(css_inherit_host(&fixture), "p", "c", NULL);
    initial_child = css_host_add(css_inherit_host(&fixture), "p", "d", NULL);
    unset_child = css_host_add(css_inherit_host(&fixture), "p", "e", NULL);
    css_host_set_parent(css_inherit_host(&fixture), em_child, div);
    css_host_set_parent(css_inherit_host(&fixture), in_child, div);
    css_host_set_parent(css_inherit_host(&fixture), inherit_child, div);
    css_host_set_parent(css_inherit_host(&fixture), initial_child, div);
    css_host_set_parent(css_inherit_host(&fixture), unset_child, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#app { font-size: 1.25rem; width: 30em }"
                      "#a { font-size: 2em }"
                      "#b { width: 1in }"
                      "#c { width: inherit }"
                      "#d { width: initial }"
                      "#e { width: unset }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&outer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&relative, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&absolute, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&inherit_len, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&initial_len, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&unset_len, NULL), ROBO_OK);

    css_unit_compute_of(&fixture, div, outer);
    css_unit_compute_of(&fixture, em_child, relative);
    css_unit_compute_of(&fixture, in_child, absolute);
    css_unit_compute_of(&fixture, inherit_child, inherit_len);
    css_unit_compute_of(&fixture, initial_child, initial_len);
    css_unit_compute_of(&fixture, unset_child, unset_len);

    /* A contextual length survives the whole pipeline as a typed length with its
     * unit live, and it is inherited unchanged. */
    css_computed_is(outer, "font-size", "1.25rem");
    css_computed_flags_are(outer, "font-size", "declared");
    css_computed_is(outer, "width", "30em");
    css_computed_is(relative, "font-size", "2em");
    css_computed_flags_are(relative, "font-size", "declared");

    /* An absolute length is normalized to px wherever it was written. */
    css_computed_is(absolute, "width", "96px");
    css_computed_flags_are(absolute, "width", "declared");

    /* `inherit` on a property that does not inherit by default pulls the
     * parent's *computed* representation — `30em`, not `30` and not `480px`. */
    css_computed_is(inherit_len, "width", "30em");
    css_computed_flags_are(inherit_len, "width", "inherited");
    /* `initial` and `unset` on a non-inherited property both land on the
     * property's own initial value, which for `width` is the keyword `auto` — a
     * *representation* rather than a resolved width. */
    css_computed_is(initial_len, "width", "auto");
    css_computed_flags_are(initial_len, "width", "default");
    css_computed_is(unset_len, "width", "auto");
    css_computed_flags_are(unset_len, "width", "default");

    /* ...while `unset` on an *inherited* property is `inherit` instead, and the
     * inherited value is the parent's computed one. */
    css_computed_is(unset_len, "font-size", "1.25rem");
    css_computed_flags_are(unset_len, "font-size", "inherited");

    robo_css_computed_style_destroy(unset_len);
    robo_css_computed_style_destroy(initial_len);
    robo_css_computed_style_destroy(inherit_len);
    robo_css_computed_style_destroy(absolute);
    robo_css_computed_style_destroy(relative);
    robo_css_computed_style_destroy(outer);
    css_inherit_end(&fixture);
}

/**
 * Numbers and colours in one stylesheet, which is the shape a real sheet has.
 *
 * 9.9 owns colours and 9.10 owns numbers, and the value layer's dispatch is what
 * keeps them from being confused: this asserts that adding the numeric vocabulary
 * did not widen or narrow what a colour-shaped property accepts, and the other
 * way round. It is the visual checkpoint's own remark — a sheet is both at once.
 */
static void test_unit_computed_coexists_with_colours(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *panel = NULL;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "panel", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#panel { color: #336699; background-color: rgb(255, 255, 255);"
                      " width: 1cm; height: 50%; font-size: 12pt }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&panel, NULL), ROBO_OK);
    css_unit_compute_of(&fixture, div, panel);

    css_computed_is(panel, "color", "#336699");
    css_computed_is(panel, "background-color", "#ffffff");
    css_computed_is(panel, "width", "37.795276px");
    /* A percentage stays a percentage, beside lengths that did convert — the
     * distinction the numeric kind exists to keep. */
    css_computed_is(panel, "height", "50%");
    css_computed_is(panel, "font-size", "16px");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 50%"),
                      "percentage");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 1cm"),
                      "length");
    /* A numeric value is not a colour and a colour is not a numeric value, which
     * is the boundary between the two modules. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: 10px"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: red"),
                      "invalid");

    robo_css_computed_style_destroy(panel);
    css_inherit_end(&fixture);
}

/**
 * The same inputs produce the same outputs, every time.
 *
 * Determinism is a property of the whole chain, so it is asserted over a
 * document rather than a value: two independent computations of one element must
 * agree property for property, and the unit table must answer identically when
 * walked in order and in reverse.
 */
static void test_unit_determinism(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *first = NULL;
    robo_css_computed_style_t *second = NULL;
    robo_css_element_ref_t div;
    static const char *const names[] = { "width", "height", "font-size", "color",
                                        "background-color" };
    size_t index;
    double px = 0.0;
    int reverse;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "app", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#app { width: 2.5cm; height: 10vh; font-size: 1.5rem;"
                      " color: #0e7aff; background-color: rgba(0, 0, 0, 0.25) }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&first, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&second, NULL), ROBO_OK);
    css_unit_compute_of(&fixture, div, first);
    css_unit_compute_of(&fixture, div, second);

    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        char one[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
        char two[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

        css_computed_note(first, names[index], one, sizeof(one));
        css_computed_note(second, names[index], two, sizeof(two));
        ROBO_CHECK_STR_EQ(one, two);
    }

    robo_css_computed_style_destroy(second);
    robo_css_computed_style_destroy(first);
    css_inherit_end(&fixture);

    /* The vocabulary answers the same walked backwards, so nothing in it is
     * order-dependent. */
    for (reverse = (int)ROBO_CSS_UNIT_COUNT - 1; reverse >= 0; reverse--) {
        robo_css_unit_t unit = (robo_css_unit_t)reverse;
        double forward_px = 0.0;
        double backward_px = 0.0;

        ROBO_CHECK_EQ_INT((int)robo_css_unit_resolve_px(3.0, unit, NULL, &forward_px),
                          (int)robo_css_unit_resolve_px(3.0, unit, NULL, &backward_px));
        ROBO_CHECK(css_unit_near(forward_px, backward_px, 1e-12));
    }
    ROBO_CHECK_EQ_INT(robo_css_unit_resolve_px(1.0, ROBO_CSS_UNIT_IN, NULL, &px),
                      ROBO_CSS_LENGTH_RESOLVED);
    ROBO_CHECK(css_unit_near(px, 96.0, 1e-9));
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_unit(void);

void robo_test_register_css_unit(void)
{
    robo_test_register("css.unit.vocabulary", test_unit_vocabulary);
    robo_test_register("css.unit.absolute_conversion", test_unit_absolute_conversion);
    robo_test_register("css.unit.resolve_absolute", test_unit_resolve_absolute);
    robo_test_register("css.unit.resolve_needs_context", test_unit_resolve_needs_context);
    robo_test_register("css.unit.resolve_font_relative", test_unit_resolve_font_relative);
    robo_test_register("css.unit.resolve_viewport", test_unit_resolve_viewport);
    robo_test_register("css.unit.resolve_invalid", test_unit_resolve_invalid);
    robo_test_register("css.unit.context", test_unit_context);
    robo_test_register("css.numeric.kinds", test_numeric_kinds);
    robo_test_register("css.numeric.equality", test_numeric_equality);
    robo_test_register("css.numeric.constructors", test_numeric_constructors);
    robo_test_register("css.numeric.parse_outcomes", test_numeric_parse_outcomes);
    robo_test_register("css.numeric.write", test_numeric_write);
    robo_test_register("css.numeric.robust", test_numeric_robust);
    robo_test_register("css.unit.computed.lengths", test_unit_computed_lengths);
    robo_test_register("css.unit.computed.inheritance", test_unit_computed_inheritance);
    robo_test_register("css.unit.computed.coexists_with_colours",
                       test_unit_computed_coexists_with_colours);
    robo_test_register("css.unit.determinism", test_unit_determinism);
}
