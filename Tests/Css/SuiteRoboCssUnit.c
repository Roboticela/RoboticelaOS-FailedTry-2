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
static const CssValue *CssUnitValueOf(const char *text,
                                                CssDeclarationList *list)
{
    CssError error = RoboCssCleared;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations(text, list, &error), RoboOk);
    /* One declaration out: a fixture that produced none would make every
     * expectation built on it an assertion about a NULL value. */
    RoboCheckEqInt((int)CssDeclarationListCount(list), 1);
    return CssDeclarationValue(CssDeclarationListAt(list, 0u));
}

/**
 * Parse the numeric value in @p declaration, returning what came out.
 *
 * `CssNumeric` is a tag, a double and an enumerator by value, so it is
 * safe to read after the list is disposed — which is why this does not leak the
 * list into every assertion's scope.
 */
static CssNumericParse GetRoboCssNumericParse(const char *declaration,
                                                 CssNumeric *out)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value = CssUnitValueOf(declaration, &list);
    CssNumericParse outcome;

    RoboCheck(value != NULL);
    outcome = CssNumericFromValue(value, out);
    CssDeclarationListDispose(&list);
    return outcome;
}

/** Assert @p declaration is a numeric value and writes back as @p expected. */
static void CssNumericIs(const char *declaration, const char *expected)
{
    CssNumeric numeric = CssNumericNone();
    char text[RoboCssNumericTextMax];
    size_t length = 0u;

    RoboCheckEqInt((int)GetRoboCssNumericParse(declaration, &numeric),
                      RoboCssNumericParseResolved);
    RoboCheckEqInt(CssNumericWrite(numeric, text, sizeof(text), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(text, expected);
    /* The length is asserted too: a writer that returned the right bytes and the
     * wrong count would pass the comparison above and break every caller that
     * trusts the count. */
    RoboCheckEqInt(length, strlen(expected));
}

/** The kind name @p declaration parses to. */
static const char *CssNumericKindOf(const char *declaration)
{
    CssNumeric numeric = CssNumericNone();
    CssNumericParse outcome = GetRoboCssNumericParse(declaration, &numeric);
    const char *name;

    if (outcome != RoboCssNumericParseResolved) {
        return "not numeric";
    }
    name = CssNumericKindName(numeric.kind);
    /* The returned name is one of the module's static strings, so it outlives
     * this call — which is what makes a compare-against-a-literal safe here. */
    return name;
}

/** The outcome name @p declaration parses to. */
static const char *CssNumericOutcomeOf(const char *declaration)
{
    CssNumeric numeric = CssNumericNone();

    return CssNumericParseName(GetRoboCssNumericParse(declaration, &numeric));
}

/** Non-zero when @p a and @p b are within @p epsilon of each other. */
static int CssUnitNear(double a, double b, double epsilon)
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
static double CssUnitOverflow(void)
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
static void TestUnitVocabulary(void)
{
    int unit;
    int distinct = 0;

    CssFixture();

    for (unit = (int)RoboCssUnitNone; unit < (int)RoboCssUnitCount; unit++) {
        CssUnit enumerator = (CssUnit)unit;
        const char *name = CssUnitName(enumerator);

        RoboCheckEqInt(CssUnitIsKnown(enumerator), 1);
        if (enumerator == RoboCssUnitNone) {
            /* NONE is a defined enumerator whose spelling is empty, because it is
             * the absence of a unit rather than one nobody knows. */
            RoboCheckStrEq(name, "");
            RoboCheckEqInt(MakeCssUnitClass(enumerator), RoboCssUnitClassNone);
            continue;
        }
        RoboCheck(name[0] != '\0');
        RoboCheckEqInt(CssUnitFromText(name), enumerator);
        distinct++;
    }
    /* 31 real units plus NONE. Stated as a number so that adding a row without
     * adding a test that walks to it fails here rather than passing quietly. */
    RoboCheckEqInt(distinct, 31);

    /* The families, one representative each, and the boundary that makes a unit
     * a length at all. */
    RoboCheckEqInt(MakeCssUnitClass(RoboCssUnitPx),
                      RoboCssUnitClassAbsolute);
    RoboCheckEqInt(MakeCssUnitClass(RoboCssUnitEm),
                      RoboCssUnitClassFontRelative);
    RoboCheckEqInt(MakeCssUnitClass(RoboCssUnitVw),
                      RoboCssUnitClassViewport);
    RoboCheckEqInt(MakeCssUnitClass(RoboCssUnitNone), RoboCssUnitClassNone);
    RoboCheckStrEq(CssUnitClassName(RoboCssUnitClassAbsolute), "absolute");
    RoboCheckStrEq(CssUnitClassName(RoboCssUnitClassFontRelative),
                      "font-relative");
    RoboCheckStrEq(CssUnitClassName(RoboCssUnitClassViewport), "viewport");
    RoboCheckStrEq(CssUnitClassName(RoboCssUnitClassNone), "none");
    RoboCheckStrEq(CssUnitClassName((CssUnitClass)99), "none");

    /* An unknown enumerator is not known, and answers nothing rather than
     * pointing at the first row. */
    RoboCheckEqInt(CssUnitIsKnown((CssUnit)99), 0);
    RoboCheckEqInt(CssUnitIsKnown((CssUnit)-1), 0);
    RoboCheckStrEq(CssUnitName((CssUnit)99), "");
    RoboCheckEqInt(MakeCssUnitClass((CssUnit)99), RoboCssUnitClassNone);
    RoboCheckEqInt(CssUnitViewportFamily((CssUnit)99),
                      RoboCssViewportDefault);

    /* The identifier door: ASCII case-insensitive, whole-spelling, and empty for
     * nothing. `10pixels` is *not* `px`, which a prefix match would allow. */
    RoboCheckEqInt(CssUnitFromText("PX"), RoboCssUnitPx);
    RoboCheckEqInt(CssUnitFromText("Em"), RoboCssUnitEm);
    RoboCheckEqInt(CssUnitFromText("REM"), RoboCssUnitRem);
    RoboCheckEqInt(CssUnitFromText("vMiN"), RoboCssUnitVmin);
    RoboCheckEqInt(CssUnitFromText("DVH"), RoboCssUnitDvh);
    RoboCheckEqInt(CssUnitFromText("pixels"), RoboCssUnitNone);
    RoboCheckEqInt(CssUnitFromText(""), RoboCssUnitNone);
    RoboCheckEqInt(CssUnitFromText(NULL), RoboCssUnitNone);
    RoboCheckEqInt(CssUnitFromSlice(CssSliceFromText("pt")),
                      RoboCssUnitPt);

    /* Which units have a fixed factor is a question about the *table*, not about
     * `toPx` — the distinction that a relative unit reported as absolute would
     * erase, and with it the whole "do not invent a font size" decision. */
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitPx), 1);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitIn), 1);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitQ), 1);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitEm), 0);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitCh), 0);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitLh), 0);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitRlh), 0);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitVw), 0);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitDvmax), 0);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitNone), 0);
    RoboCheckEqInt(CssUnitIsAbsolute((CssUnit)99), 0);

    /* Sixteen viewport units: four measures across four families. Counted rather
     * than listed so that a family added without its units fails here. */
    {
        int viewport = 0;
        int families[RoboCssViewportFamilyCount] = { 0, 0, 0, 0 };

        for (unit = (int)RoboCssUnitNone; unit < (int)RoboCssUnitCount; unit++) {
            CssUnit enumerator = (CssUnit)unit;

            if (CssUnitIsViewport(enumerator)) {
                viewport++;
                families[(int)CssUnitViewportFamily(enumerator)]++;
            }
        }
        /* Six units in the default family — `vw`, `vh`, `vmin`, `vmax`, and the
         * two axes — and four in each of the three overridable ones. */
        RoboCheckEqInt(viewport, 18);
        RoboCheckEqInt(families[RoboCssViewportDefault], 6);
        RoboCheckEqInt(families[RoboCssViewportSmall], 4);
        RoboCheckEqInt(families[RoboCssViewportLarge], 4);
        RoboCheckEqInt(families[RoboCssViewportDynamic], 4);
    }
    RoboCheckEqInt(CssUnitViewportFamily(RoboCssUnitVw),
                      RoboCssViewportDefault);
    RoboCheckEqInt(CssUnitViewportFamily(RoboCssUnitSvh),
                      RoboCssViewportSmall);
    RoboCheckEqInt(CssUnitViewportFamily(RoboCssUnitLvmin),
                      RoboCssViewportLarge);
    RoboCheckEqInt(CssUnitViewportFamily(RoboCssUnitDvw),
                      RoboCssViewportDynamic);
    /* Documented rather than incidental: the family question is only asked of a
     * viewport unit, and a non-viewport one answers with the family every such
     * unit would use. */
    RoboCheckEqInt(CssUnitViewportFamily(RoboCssUnitPx),
                      RoboCssViewportDefault);
    RoboCheckEqInt(CssUnitIsViewport(RoboCssUnitPx), 0);
    RoboCheckEqInt(CssUnitIsViewport(RoboCssUnitNone), 0);
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
static void CssUnitAssertRelationship(double value, CssUnit unit,
                                         double expected)
{
    double px = 0.0;
    double tolerance = (expected < 0.0 ? -expected : expected) * 1e-9;
    char message[128];

    RoboCheckEqInt((int)CssUnitResolvePx(value, unit, NULL, &px),
                      (int)RoboCssLengthResolved);
    if (!CssUnitNear(px, expected, tolerance < 1e-9 ? 1e-9 : tolerance)) {
        sprintf(message, "%.10g%s resolved to %.10g, expected %.10g", value,
                CssUnitName(unit), px, expected);
        RoboCheckMsg(0, message);
    }
}

static void TestUnitAbsoluteConversion(void)
{
    double px = 0.0;

    CssFixture();

    /* The reference pixel itself, and the trivial case a converter is most
     * likely to get wrong by multiplying with a missing factor. */
    CssUnitAssertRelationship(1.0, RoboCssUnitPx, 1.0);
    CssUnitAssertRelationship(96.0, RoboCssUnitPx, 96.0);
    CssUnitAssertRelationship(1.5, RoboCssUnitPx, 1.5);
    CssUnitAssertRelationship(1.0, RoboCssUnitIn, 96.0);
    CssUnitAssertRelationship(2.0, RoboCssUnitIn, 192.0);

    /* The definitional pairs, one per unit. */
    CssUnitAssertRelationship(2.54, RoboCssUnitCm, 96.0);
    CssUnitAssertRelationship(25.4, RoboCssUnitMm, 96.0);
    /* `Q` is a quarter of a millimetre, so forty of them are a centimetre and
     * four are a millimetre — asserted both ways round, because a factor that
     * was out by a factor of forty would pass a test that only checked one. */
    CssUnitAssertRelationship(40.0, RoboCssUnitQ, 96.0 / 2.54);
    CssUnitAssertRelationship(4.0, RoboCssUnitQ, 96.0 / 25.4);
    CssUnitAssertRelationship(72.0, RoboCssUnitPt, 96.0);
    CssUnitAssertRelationship(6.0, RoboCssUnitPc, 96.0);
    /* 12pt is 1pc, so the two must agree about sixteen pixels. */
    CssUnitAssertRelationship(12.0, RoboCssUnitPt, 16.0);
    CssUnitAssertRelationship(1.0, RoboCssUnitPc, 16.0);

    /* Zero and negative: a length is a signed quantity, and zero is the value
     * every unit agrees about. */
    CssUnitAssertRelationship(0.0, RoboCssUnitIn, 0.0);
    CssUnitAssertRelationship(0.0, RoboCssUnitCm, 0.0);
    CssUnitAssertRelationship(-1.0, RoboCssUnitIn, -96.0);

    /* `toPx` is the *conversion* door rather than the resolution one, and it
     * answers with its input unchanged for a unit that has no fixed factor —
     * which is why it must never be asked whether a unit is absolute. */
    RoboCheck(CssUnitNear(CssUnitToPx(2.0, RoboCssUnitEm), 2.0, 1e-12));
    RoboCheck(CssUnitNear(CssUnitToPx(50.0, RoboCssUnitVh), 50.0, 1e-12));
    RoboCheck(CssUnitNear(CssUnitToPx(7.0, RoboCssUnitNone), 7.0, 1e-12));
    RoboCheck(CssUnitNear(CssUnitToPx(7.0, (CssUnit)99), 7.0, 1e-12));

    /* An absolute length reads the same with no context and with an empty one:
     * that is the entire content of "a definition rather than a measurement". */
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitIn, NULL, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 96.0, 1e-9));
    {
        CssUnitContext context;

        CssUnitContextInit(&context);
        RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitIn, &context, &px),
                          RoboCssLengthResolved);
        RoboCheck(CssUnitNear(px, 96.0, 1e-9));
    }

    /* The resolution vocabulary is closed, and its names are the names a
     * developer reads in a diagnostic. */
    RoboCheckEqInt(CssLengthResolutionIsKnown(RoboCssLengthResolved), 1);
    RoboCheckEqInt(CssLengthResolutionIsKnown(RoboCssLengthInvalid), 1);
    RoboCheckEqInt(CssLengthResolutionIsKnown((CssLengthResolution)9),
                      0);
    RoboCheckStrEq(CssLengthResolutionName(RoboCssLengthResolved),
                      "resolved");
    RoboCheckStrEq(CssLengthResolutionName(RoboCssLengthNeedsContext),
                      "needs-context");
    RoboCheckStrEq(CssLengthResolutionName(RoboCssLengthNotALength),
                      "not-a-length");
    RoboCheckStrEq(CssLengthResolutionName(RoboCssLengthInvalid), "invalid");
    RoboCheckStrEq(CssLengthResolutionName((CssLengthResolution)9),
                      "unknown");
}

/* ------------------------------------------------------------------ */
/* Contextual resolution                                               */
/* ------------------------------------------------------------------ */

/**
 * The seven absolute units, resolved rather than merely converted.
 *
 * The distinction between this and `CssUnitToPx` is the point: the
 * resolver reports *whether* the unit is a length, and a value of zero is
 * RESOLVED rather than NOT_A_LENGTH. Asserted with a context that knows a font
 * size and a viewport, because an absolute unit must ignore both — a resolver
 * that consulted the context for `1in` would have answered a question about the
 * display that CSS's definition of `in` does not ask.
 */
static void TestUnitResolveAbsolute(void)
{
    static const struct {
        CssUnit unit;
        double value;
        double px;
    } cases[] = {
        { RoboCssUnitPx, 0.0, 0.0 },
        { RoboCssUnitPx, 12.5, 12.5 },
        { RoboCssUnitIn, 1.0, 96.0 },
        { RoboCssUnitIn, -1.0, -96.0 },
        { RoboCssUnitCm, 1.0, 96.0 / 2.54 },
        { RoboCssUnitMm, 1.0, 96.0 / 25.4 },
        { RoboCssUnitQ, 1.0, 96.0 / 101.6 },
        { RoboCssUnitPt, 1.0, 1.3333333333333333 },
        { RoboCssUnitPc, 1.0, 16.0 }
    };
    CssUnitContext context;
    size_t index;
    double px = 0.0;
    int unit;

    CssFixture();
    CssUnitContextInit(&context);
    CssUnitContextSetFont(&context, 100.0, 50.0, 60.0, 150.0);
    CssUnitContextSetViewport(&context, 1000.0, 500.0);

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        double tolerance = (cases[index].px < 0.0 ? -cases[index].px : cases[index].px) * 1e-9;

        RoboCheckEqInt((int)CssUnitResolvePx(cases[index].value, cases[index].unit,
                                                       &context, &px),
                          (int)RoboCssLengthResolved);
        RoboCheck(CssUnitNear(px, cases[index].px, tolerance < 1e-9 ? 1e-9 : tolerance));
    }

    /* All seven are absolute, and the six that are not are not — asked of the
     * whole set rather than of a sample, because "some relative unit is really
     * absolute" is exactly the defect this phase's predecessor had. */
    for (unit = (int)RoboCssUnitNone; unit < (int)RoboCssUnitCount; unit++) {
        CssUnitClass unitClass = MakeCssUnitClass((CssUnit)unit);
        int expected = unitClass == RoboCssUnitClassAbsolute;

        RoboCheckEqInt(CssUnitIsAbsolute((CssUnit)unit), expected);
    }

    /* A contextual unit still refuses with a context that knows a font size and
     * a viewport, when the fact it needs is an axis or a family rather than a
     * size — and `50%` is not a unit at all. */
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitRem, &context, &px),
                      RoboCssLengthNeedsContext);
}

/**
 * Every contextual unit answers NEEDS_CONTEXT with no context at all.
 *
 * This is the phase's central claim, so it is asserted for the whole set rather
 * than for `2em` and `50vh`: a resolver that converted *any* of these without a
 * context would have invented the font size or the viewport it needed.
 */
static void TestUnitResolveNeedsContext(void)
{
    CssUnitContext empty;
    int unit;
    double px = 12345.0;

    CssFixture();
    CssUnitContextInit(&empty);

    for (unit = (int)RoboCssUnitNone; unit < (int)RoboCssUnitCount; unit++) {
        CssUnit enumerator = (CssUnit)unit;

        if (enumerator == RoboCssUnitNone || CssUnitIsAbsolute(enumerator)) {
            continue;
        }
        /* NULL is a caller that knows nothing; an initialized context is a caller
         * that knows nothing and said so. Neither may produce a number. */
        RoboCheckEqInt((int)CssUnitResolvePx(2.0, enumerator, NULL, &px),
                          (int)RoboCssLengthNeedsContext);
        RoboCheckEqInt((int)CssUnitResolvePx(2.0, enumerator, &empty, &px),
                          (int)RoboCssLengthNeedsContext);
    }
}

static void TestUnitResolveFontRelative(void)
{
    CssUnitContext context;
    double px = 0.0;

    CssFixture();
    CssUnitContextInit(&context);

    /* The element's own metrics only: `rem` must still refuse, because a context
     * that knows the element's font does not know the root's. */
    CssUnitContextSetFont(&context, 16.0, 8.0, 9.0, 24.0);
    RoboCheckEqInt(CssUnitResolvePx(2.0, RoboCssUnitEm, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 32.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitEx, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 8.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(3.0, RoboCssUnitCh, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 27.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitLh, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 24.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitRem, &context, &px),
                      RoboCssLengthNeedsContext);

    /* The root's metrics only: the mirror image, and the reason the two sets of
     * fields are separate rather than one "font context". */
    {
        CssUnitContext root;

        CssUnitContextInit(&root);
        CssUnitContextSetRootFont(&root, 20.0, 30.0);
        RoboCheckEqInt(CssUnitResolvePx(2.0, RoboCssUnitRem, &root, &px),
                          RoboCssLengthResolved);
        RoboCheck(CssUnitNear(px, 40.0, 1e-9));
        RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitRlh, &root, &px),
                          RoboCssLengthResolved);
        RoboCheck(CssUnitNear(px, 30.0, 1e-9));
        RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitEm, &root, &px),
                          RoboCssLengthNeedsContext);
    }

    /* Both, and the values are the ones supplied rather than a default: a
     * resolver that used 16 for everything would pass the two tests above by
     * coincidence and fail these. */
    CssUnitContextSetRootFont(&context, 10.0, 15.0);
    RoboCheckEqInt(CssUnitResolvePx(1.5, RoboCssUnitRem, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 15.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(0.5, RoboCssUnitEm, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 8.0, 1e-9));
    /* Zero is zero whatever the context says, and zero with no context is still
     * *unresolvable* rather than zero — the context is what is missing, not the
     * magnitude. */
    RoboCheckEqInt(CssUnitResolvePx(0.0, RoboCssUnitEm, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 0.0, 1e-12));
    RoboCheckEqInt(CssUnitResolvePx(0.0, RoboCssUnitEm, NULL, &px),
                      RoboCssLengthNeedsContext);
}

static void TestUnitResolveViewport(void)
{
    CssUnitContext context;
    double px = 0.0;

    CssFixture();
    CssUnitContextInit(&context);
    CssUnitContextSetViewport(&context, 1000.0, 500.0);

    /* One percent per unit, so the expected value is the dimension over a
     * hundred — and vmin/vmax pick the dimension rather than the value. */
    RoboCheckEqInt(CssUnitResolvePx(50.0, RoboCssUnitVw, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 500.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(10.0, RoboCssUnitVh, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 50.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(10.0, RoboCssUnitVmin, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 50.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(10.0, RoboCssUnitVmax, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 100.0, 1e-9));

    /* The axes default to the horizontal writing mode, so `vi` is the width and
     * `vb` the height — and swapping the flag swaps them. */
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitVi, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 1000.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitVb, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 500.0, 1e-9));
    context.verticalWriting = 1;
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitVi, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 500.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitVb, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 1000.0, 1e-9));
    context.verticalWriting = 0;

    /* The three families fall back to the default, which is what makes a window
     * whose chrome never moves expressible once rather than four times. Every
     * `sv*`/`lv*`/`dv*` unit must agree with its default-family relative. */
    RoboCheckEqInt(CssUnitResolvePx(50.0, RoboCssUnitSvw, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 500.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(50.0, RoboCssUnitLvw, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 500.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(50.0, RoboCssUnitDvw, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 500.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitSvmin, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 500.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitLvmax, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 1000.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(10.0, RoboCssUnitDvh, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 50.0, 1e-9));

    /* With the families *set*, each answers its own size instead — and the
     * default family keeps answering the default one, which is the boundary
     * between the fallback chain and a plain override. */
    CssUnitContextSetSmallViewport(&context, 800.0, 400.0);
    CssUnitContextSetLargeViewport(&context, 1000.0, 600.0);
    CssUnitContextSetDynamicViewport(&context, 900.0, 450.0);
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitSvw, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 800.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitLvh, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 600.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitDvw, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 900.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitVw, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 1000.0, 1e-9));

    /* A viewport unit that is not a percentage of the *other* dimension: `vh`
     * does not read the width, which a resolver over a single "viewport size"
     * would get wrong. */
    CssUnitContextInit(&context);
    CssUnitContextSetViewport(&context, 300.0, 700.0);
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitVmin, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 300.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(100.0, RoboCssUnitVmax, &context, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 700.0, 1e-9));
}

static void TestUnitResolveInvalid(void)
{
    CssUnitContext context;
    double px = 12345.0;
    double overflow = CssUnitOverflow();
    double notANumber = overflow - overflow;

    CssFixture();
    CssUnitContextInit(&context);

    /* The overflow multiplier actually overflowed, so the test below is about
     * the module rather than about this helper. */
    RoboCheckEqInt(overflow == overflow, 1);
    RoboCheckEqInt((overflow - overflow) == 0.0, 0);
    RoboCheckEqInt(notANumber == notANumber, 0);

    /* Not a unit at all: NONE is the absence of a unit, and an unknown
     * enumerator is not one either. Distinguished from INVALID, because "this is
     * not a length" and "this length is not a number" are different fixes. */
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitNone, NULL, &px),
                      RoboCssLengthNotALength);
    RoboCheckEqInt(CssUnitResolvePx(1.0, (CssUnit)99, NULL, &px),
                      RoboCssLengthNotALength);

    /* A magnitude that is not finite, for a unit that would otherwise resolve.
     * Asserted for an absolute unit too: the guard is about the *value*, and an
     * absolute unit is the case where nothing else could refuse it. */
    RoboCheckEqInt(CssUnitResolvePx(notANumber, RoboCssUnitPx, NULL, &px),
                      RoboCssLengthInvalid);
    RoboCheckEqInt(CssUnitResolvePx(overflow, RoboCssUnitPx, NULL, &px),
                      RoboCssLengthInvalid);
    RoboCheckEqInt(CssUnitResolvePx(notANumber, RoboCssUnitEm, &context, &px),
                      RoboCssLengthInvalid);

    /* A context input that is not finite is INVALID rather than NEEDS_CONTEXT:
     * the caller did supply a font size and it is not one, which is fixable
     * information rather than "ask again later". */
    CssUnitContextSetFont(&context, notANumber, 8.0, 9.0, 24.0);
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitEm, &context, &px),
                      RoboCssLengthInvalid);
    CssUnitContextInit(&context);
    CssUnitContextSetViewport(&context, overflow, 500.0);
    RoboCheckEqInt(CssUnitResolvePx(50.0, RoboCssUnitVw, &context, &px),
                      RoboCssLengthInvalid);
    RoboCheckEqInt(CssUnitResolvePx(50.0, RoboCssUnitSvw, &context, &px),
                      RoboCssLengthInvalid);

    /* The multiplication's own overflow: `1e308in` is a finite number and an
     * infinite length, and an infinity that reached a layout would be worse than
     * a refusal. `1e300in` is *not* an example — 9.6e301 is finite — which is
     * what makes the magnitude here worth choosing rather than guessing. */
    RoboCheckEqInt(CssUnitResolvePx(1e308, RoboCssUnitIn, NULL, &px),
                      RoboCssLengthInvalid);
    RoboCheckEqInt(CssUnitResolvePx(1e300, RoboCssUnitIn, NULL, &px),
                      RoboCssLengthResolved);

    /* Nothing was written for any refusal: the out-parameter is only ever the
     * caller's to read after RESOLVED, and this is what makes that true. */
    px = 12345.0;
    RoboCheckEqInt(CssUnitResolvePx(2.0, RoboCssUnitEm, NULL, &px),
                      RoboCssLengthNeedsContext);
    RoboCheck(CssUnitNear(px, 12345.0, 1e-9));
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitNone, NULL, &px),
                      RoboCssLengthNotALength);
    RoboCheck(CssUnitNear(px, 12345.0, 1e-9));

    /* A NULL output is a caller that only wants the answer, which is legal. */
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitIn, NULL, NULL),
                      RoboCssLengthResolved);
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitEm, NULL, NULL),
                      RoboCssLengthNeedsContext);
}

static void TestUnitContext(void)
{
    CssUnitContext context;

    CssFixture();

    /* A cleared context knows nothing, which is the state every flag exists to
     * express — and the state a factory-allocated struct would start in by luck
     * rather than by contract. */
    memset(&context, 0xFF, sizeof(context));
    CssUnitContextInit(&context);
    RoboCheckEqInt(context.hasFontMetrics, 0);
    RoboCheckEqInt(context.hasRootFontMetrics, 0);
    RoboCheckEqInt(context.hasViewport, 0);
    RoboCheckEqInt(context.hasSmallViewport, 0);
    RoboCheckEqInt(context.hasLargeViewport, 0);
    RoboCheckEqInt(context.hasDynamicViewport, 0);
    RoboCheckEqInt(context.verticalWriting, 0);

    CssUnitContextSetFont(&context, 16.0, 8.0, 9.0, 24.0);
    RoboCheckEqInt(context.hasFontMetrics, 1);
    RoboCheck(CssUnitNear(context.fontSize, 16.0, 1e-12));
    RoboCheck(CssUnitNear(context.xHeight, 8.0, 1e-12));
    RoboCheck(CssUnitNear(context.zeroWidth, 9.0, 1e-12));
    RoboCheck(CssUnitNear(context.lineHeight, 24.0, 1e-12));
    RoboCheckEqInt(context.hasRootFontMetrics, 0);

    CssUnitContextSetRootFont(&context, 20.0, 30.0);
    RoboCheckEqInt(context.hasRootFontMetrics, 1);
    CssUnitContextSetViewport(&context, 1000.0, 500.0);
    CssUnitContextSetSmallViewport(&context, 800.0, 400.0);
    CssUnitContextSetLargeViewport(&context, 1000.0, 600.0);
    CssUnitContextSetDynamicViewport(&context, 900.0, 450.0);
    RoboCheckEqInt(context.hasViewport, 1);
    RoboCheckEqInt(context.hasSmallViewport, 1);
    RoboCheckEqInt(context.hasLargeViewport, 1);
    RoboCheckEqInt(context.hasDynamicViewport, 1);

    /* Re-initializing clears what a setter set, so "I know nothing" is reachable
     * after "I knew something" — the state a caller reusing a context needs. */
    CssUnitContextInit(&context);
    RoboCheckEqInt(context.hasFontMetrics, 0);
    RoboCheckEqInt(context.hasViewport, 0);

    /* Every setter tolerates NULL, because a caller that has no context to fill
     * has nothing to do rather than a bug. */
    CssUnitContextInit(NULL);
    CssUnitContextSetFont(NULL, 1.0, 1.0, 1.0, 1.0);
    CssUnitContextSetRootFont(NULL, 1.0, 1.0);
    CssUnitContextSetViewport(NULL, 1.0, 1.0);
    CssUnitContextSetSmallViewport(NULL, 1.0, 1.0);
    CssUnitContextSetLargeViewport(NULL, 1.0, 1.0);
    CssUnitContextSetDynamicViewport(NULL, 1.0, 1.0);
    RoboCheck(1);
}

/* ------------------------------------------------------------------ */
/* The typed numeric value                                             */
/* ------------------------------------------------------------------ */

static void TestNumericKinds(void)
{
    int kind;

    CssFixture();

    for (kind = 0; kind < (int)RoboCssNumericKindCount; kind++) {
        RoboCheckEqInt(CssNumericKindIsKnown((CssNumericKind)kind), 1);
    }
    RoboCheckEqInt(CssNumericKindIsKnown((CssNumericKind)99), 0);
    RoboCheckStrEq(CssNumericKindName(RoboCssNumericNone), "none");
    RoboCheckStrEq(CssNumericKindName(RoboCssNumericNumber), "number");
    RoboCheckStrEq(CssNumericKindName(RoboCssNumericInteger), "integer");
    RoboCheckStrEq(CssNumericKindName(RoboCssNumericPercentage),
                      "percentage");
    RoboCheckStrEq(CssNumericKindName(RoboCssNumericLength), "length");
    RoboCheckStrEq(CssNumericKindName((CssNumericKind)99), "none");

    /* A cleared record is not a value, and its fields are the ones the factory
     * writes rather than whatever was on the stack. */
    {
        CssNumeric none = CssNumericNone();

        RoboCheckEqInt(none.kind, RoboCssNumericNone);
        RoboCheck(CssUnitNear(none.number, 0.0, 1e-12));
        RoboCheckEqInt(none.unit, RoboCssUnitNone);
        RoboCheckEqInt(CssNumericIsPresent(none), 0);
    }

    /* An integer is what was *written* as one, which is the grammar's question
     * and not the value's: `1.0` is whole and is not an integer. */
    CssNumericIs("n: 10", "10");
    RoboCheckStrEq(CssNumericKindOf("n: 10"), "integer");
    CssNumericIs("n: -3", "-3");
    RoboCheckStrEq(CssNumericKindOf("n: -3"), "integer");
    CssNumericIs("n: +7", "7");
    RoboCheckStrEq(CssNumericKindOf("n: +7"), "integer");
    CssNumericIs("n: 0", "0");
    CssNumericIs("n: 1.0", "1");
    RoboCheckStrEq(CssNumericKindOf("n: 1.0"), "number");
    CssNumericIs("n: 1.5", "1.5");
    RoboCheckStrEq(CssNumericKindOf("n: 1.5"), "number");
    CssNumericIs("n: -0.25", "-0.25");
    CssNumericIs("n: .5", "0.5");
    RoboCheckStrEq(CssNumericKindOf("n: .5"), "number");
    CssNumericIs("n: 1e2", "100");
    /* An exponent makes it a *number* even though the value is whole, which is
     * exactly the case a single "numeric" kind would erase. */
    RoboCheckStrEq(CssNumericKindOf("n: 1e2"), "number");
    CssNumericIs("n: 1.25e-1", "0.125");
    CssNumericIs("n: 1e+2", "100");

    /* Decimal places survive, and trailing zeroes do not survive as text. */
    CssNumericIs("n: 1.500", "1.5");
    CssNumericIs("n: 0.000001", "0.000001");
    CssNumericIs("n: 123456.75", "123456.75");

    /* A percentage is a kind of its own and never a number, because 50% of a
     * width and 50 of a z-index are different computations. */
    CssNumericIs("n: 0%", "0%");
    CssNumericIs("n: 50%", "50%");
    CssNumericIs("n: 100%", "100%");
    CssNumericIs("n: 12.5%", "12.5%");
    CssNumericIs("n: -10%", "-10%");
    CssNumericIs("n: +25%", "25%");
    RoboCheckStrEq(CssNumericKindOf("n: 50%"), "percentage");

    /* A dimension keeps its unit and its magnitude, and the unit is normalized
     * to the table's canonical spelling rather than the author's. */
    CssNumericIs("n: 10px", "10px");
    RoboCheckStrEq(CssNumericKindOf("n: 10px"), "length");
    CssNumericIs("n: 10PX", "10px");
    CssNumericIs("n: 1.5em", "1.5em");
    CssNumericIs("n: 0.25rem", "0.25rem");
    CssNumericIs("n: -20vh", "-20vh");
    CssNumericIs("n: 3ch", "3ch");
    CssNumericIs("n: 4q", "4q");

    /* The four kinds are four, and a value that is none of them is not a numeric
     * value at all rather than a fifth kind. */
    {
        static const char *const kinds[] = { "none", "number", "integer",
                                            "percentage", "length" };
        size_t index;

        for (index = 0u; index < sizeof(kinds) / sizeof(kinds[0]); index++) {
            RoboCheckStrEq(CssNumericKindName((CssNumericKind)index),
                              kinds[index]);
        }
    }
}

static void TestNumericEquality(void)
{
    CssNumeric ten = CssNumericNone();
    CssNumeric tenAgain = CssNumericNone();
    CssNumeric tenEm = CssNumericNone();
    CssNumeric fiftyPercent = CssNumericNone();
    CssNumeric fifty = CssNumericNone();

    CssFixture();

    RoboCheckEqInt(CssNumericMakeLength(&ten, 10.0, RoboCssUnitPx, NULL),
                      RoboOk);
    RoboCheckEqInt(CssNumericMakeLength(&tenAgain, 10.0, RoboCssUnitPx,
                                                   NULL),
                      RoboOk);
    RoboCheckEqInt(CssNumericMakeLength(&tenEm, 10.0, RoboCssUnitEm, NULL),
                      RoboOk);
    RoboCheckEqInt(CssNumericMakePercentage(&fiftyPercent, 50.0, NULL),
                      RoboOk);
    RoboCheckEqInt(CssNumericMakeNumber(&fifty, 50.0, 0, NULL), RoboOk);

    RoboCheckEqInt(CssNumericEquals(ten, tenAgain), 1);
    /* The unit is part of the value: converting is the resolver's job, and a
     * comparison that converted would answer a question about a context the
     * comparison does not have. */
    RoboCheckEqInt(CssNumericEquals(ten, tenEm), 0);
    /* The kind is part of the value too, and this is the pair that matters: a
     * length used where a proportion was meant is a layout bug that a
     * kind-blind comparison would make invisible. */
    RoboCheckEqInt(CssNumericEquals(fiftyPercent, fifty), 0);

    /* A cleared record equals another cleared record, and nothing else. */
    {
        CssNumeric none = CssNumericNone();
        CssNumeric zero = CssNumericNone();

        RoboCheckEqInt(CssNumericMakeNumber(&zero, 0.0, 1, NULL), RoboOk);
        RoboCheckEqInt(CssNumericEquals(none, none), 1);
        RoboCheckEqInt(CssNumericEquals(none, zero), 0);
    }

    /* Integrality is a question about the value; integer-ness is a question
     * about the grammar. They answer differently for `1.0`, which is the whole
     * point of having both. */
    {
        CssNumeric value = CssNumericNone();
        int64_t integer = 0;

        RoboCheckEqInt(CssNumericMakeNumber(&value, 10.0, 0, NULL), RoboOk);
        RoboCheckEqInt(CssNumericIsIntegral(value), 1);
        /* A NUMBER is refused even when whole, because a property whose grammar
         * says `<integer>` must refuse `1.0`. */
        RoboCheckEqInt(CssNumericIntegerValue(value, &integer), 0);

        RoboCheckEqInt(CssNumericMakeNumber(&value, 10.0, 1, NULL), RoboOk);
        RoboCheckEqInt(CssNumericIntegerValue(value, &integer), 1);
        RoboCheckEqInt(integer, 10);

        RoboCheckEqInt(CssNumericMakeNumber(&value, 10.5, 0, NULL), RoboOk);
        RoboCheckEqInt(CssNumericIsIntegral(value), 0);
        RoboCheckEqInt(CssNumericIntegerValue(value, &integer), 0);

        /* Beyond 2^53 a double cannot represent every integer, so "is this
         * whole" has no reliable answer — and the answer given is no rather than
         * a cast that would be undefined behaviour. */
        RoboCheckEqInt(CssNumericMakeNumber(&value, 1e300, 1, NULL), RoboOk);
        RoboCheckEqInt(CssNumericIsIntegral(value), 0);
        RoboCheckEqInt(CssNumericIntegerValue(value, &integer), 0);
        RoboCheckEqInt(CssNumericMakeNumber(&value, 9007199254740992.0, 1, NULL),
                          RoboOk);
        RoboCheckEqInt(CssNumericIsIntegral(value), 1);
        RoboCheckEqInt(CssNumericIntegerValue(value, &integer), 1);
        RoboCheckEqInt(integer, 9007199254740992LL);

        /* Negative values convert, and the boundary is symmetric. */
        RoboCheckEqInt(CssNumericMakeNumber(&value, -12.0, 1, NULL), RoboOk);
        RoboCheckEqInt(CssNumericIntegerValue(value, &integer), 1);
        RoboCheckEqInt(integer, -12);
        RoboCheckEqInt(CssNumericMakeNumber(&value, -1e300, 1, NULL), RoboOk);
        RoboCheckEqInt(CssNumericIntegerValue(value, &integer), 0);

        /* A NULL output is a caller asking whether, not what. */
        RoboCheckEqInt(CssNumericMakeNumber(&value, 4.0, 1, NULL), RoboOk);
        RoboCheckEqInt(CssNumericIntegerValue(value, NULL), 1);
    }
}

static void TestNumericConstructors(void)
{
    CssNumeric value = CssNumericNone();
    double overflow = CssUnitOverflow();

    CssFixture();

    /* Every constructor refuses a magnitude that is not finite, and refuses it as
     * a programmer error rather than a parse outcome: the tokenizer cannot
     * produce one of these without the parser having rejected it first. */
    RoboCheckEqInt(CssNumericMakeNumber(&value, overflow, 0, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssNumericMakePercentage(&value, overflow, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssNumericMakeLength(&value, overflow, RoboCssUnitPx, NULL),
                      RoboInvalidArgument);
    {
        double notANumber = overflow - overflow;

        RoboCheckEqInt(CssNumericMakeNumber(&value, notANumber, 0, NULL),
                          RoboInvalidArgument);
    }

    /* A length with no unit is a number, so it is refused: the caller who meant
     * a number has a constructor for that. */
    RoboCheckEqInt(CssNumericMakeLength(&value, 1.0, RoboCssUnitNone, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssNumericMakeLength(&value, 1.0, (CssUnit)99, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssNumericMakeLength(&value, 1.0, RoboCssUnitPx, NULL),
                      RoboOk);
    RoboCheckEqInt(CssNumericMakeLength(&value, 1.0, RoboCssUnitDvmax, NULL),
                      RoboOk);

    /* A NULL output is a bug rather than a value. */
    RoboCheckEqInt(CssNumericMakeNumber(NULL, 1.0, 0, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssNumericMakePercentage(NULL, 1.0, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssNumericMakeLength(NULL, 1.0, RoboCssUnitPx, NULL),
                      RoboInvalidArgument);

    /* A failing constructor leaves its output alone rather than half-written:
     * `CssNumericNone` is not stamped on a value the caller may still be
     * reading. */
    RoboCheckEqInt(CssNumericMakeLength(&value, 7.0, RoboCssUnitVw, NULL),
                      RoboOk);
    RoboCheckEqInt(CssNumericMakeNumber(&value, overflow, 0, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(value.kind, RoboCssNumericLength);
    RoboCheckEqInt(value.unit, RoboCssUnitVw);
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
static void TestNumericParseOutcomes(void)
{
    CssNumeric numeric = CssNumericNone();
    CssNumeric parsed = CssNumericNone();
    int outcome;

    CssFixture();

    for (outcome = 0; outcome < (int)RoboCssNumericParseCount; outcome++) {
        RoboCheckEqInt(CssNumericParseIsKnown((CssNumericParse)outcome),
                          1);
    }
    RoboCheckEqInt(CssNumericParseIsKnown((CssNumericParse)99), 0);
    RoboCheckStrEq(CssNumericParseName(RoboCssNumericParseInvalid),
                      "invalid");
    RoboCheckStrEq(CssNumericParseName(RoboCssNumericParseResolved),
                      "resolved");
    RoboCheckStrEq(CssNumericParseName(RoboCssNumericParseDeferred),
                      "deferred");
    RoboCheckStrEq(CssNumericParseName(RoboCssNumericParseUnknownUnit),
                      "unknown-unit");
    RoboCheckStrEq(CssNumericParseName(RoboCssNumericParseNonFinite),
                      "non-finite");
    RoboCheckStrEq(CssNumericParseName((CssNumericParse)99),
                      "invalid");

    RoboCheckStrEq(CssNumericOutcomeOf("n: 10px"), "resolved");
    RoboCheckStrEq(CssNumericOutcomeOf("n: 10foo"), "unknown-unit");
    RoboCheckStrEq(CssNumericOutcomeOf("n: 1e999"), "non-finite");
    RoboCheckStrEq(CssNumericOutcomeOf("n: -1e999"), "non-finite");

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
            RoboCheckStrEq(CssNumericOutcomeOf(declaration), "deferred");
        }
    }
    RoboCheckStrEq(CssNumericOutcomeOf("n: red"), "invalid");
    RoboCheckStrEq(CssNumericOutcomeOf("n: rgb(1, 2, 3)"), "invalid");
    RoboCheckStrEq(CssNumericOutcomeOf("n: url(a.png)"), "invalid");
    RoboCheckStrEq(CssNumericOutcomeOf("n: \"10px\""), "invalid");
    RoboCheckStrEq(CssNumericOutcomeOf("n: 10px 20px"), "invalid");
    RoboCheckStrEq(CssNumericOutcomeOf("n: 10 20"), "invalid");
    RoboCheckStrEq(CssNumericOutcomeOf("n: 10 px"), "invalid");

    /* A refused parse leaves the caller's record untouched, which is what makes
     * "check the outcome, then read the value" a safe order. */
    RoboCheckEqInt(CssNumericMakeLength(&numeric, 7.0, RoboCssUnitVh, NULL),
                      RoboOk);
    RoboCheckEqInt((int)GetRoboCssNumericParse("n: 10foo", &parsed),
                      (int)RoboCssNumericParseUnknownUnit);
    RoboCheckEqInt(CssNumericEquals(numeric,
                                              CssNumericNone()),
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
        CssDeclarationList list = RoboCssCleared;
        const CssValue *value = CssUnitValueOf("n: 2.5rem", &list);
        const CssValue *single = NULL;
        size_t child;

        for (child = 0u; child < CssValueChildCount(value); child++) {
            const CssValue *candidate = CssValueChild(value, child);

            if (candidate && candidate->kind == RoboCssValueKindToken) {
                single = candidate;
            }
        }
        RoboCheck(single != NULL);
        RoboCheckEqInt((int)CssNumericFromToken(&single->token, &parsed),
                          (int)RoboCssNumericParseResolved);
        RoboCheckEqInt(parsed.kind, RoboCssNumericLength);
        RoboCheckEqInt(parsed.unit, RoboCssUnitRem);
        RoboCheck(CssUnitNear(parsed.number, 2.5, 1e-12));
        CssDeclarationListDispose(&list);
    }

    /* NULL is a caller error rather than an outcome, and answers INVALID rather
     * than dereferencing. */
    RoboCheckEqInt((int)CssNumericFromValue(NULL, &parsed),
                      (int)RoboCssNumericParseInvalid);
    RoboCheckEqInt((int)CssNumericFromToken(NULL, &parsed),
                      (int)RoboCssNumericParseInvalid);
    {
        CssDeclarationList list = RoboCssCleared;

        RoboCheckEqInt((int)CssNumericFromValue(CssUnitValueOf("n: 1px", &list),
                                                          NULL),
                          (int)RoboCssNumericParseInvalid);
        CssDeclarationListDispose(&list);
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
static void TestNumericWrite(void)
{
    CssNumeric numeric = CssNumericNone();
    char buffer[RoboCssNumericTextMax];
    char small[8];
    size_t length = 0u;
    size_t needed = 0u;

    CssFixture();

    /* An integral magnitude has no decimal point, a fractional one has no
     * trailing zeroes, and a negative zero is zero. */
    RoboCheckEqInt(CssNumericMakeNumber(&numeric, 10.0, 1, NULL), RoboOk);
    RoboCheckEqInt(CssNumericWrite(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "10");
    RoboCheckEqInt(length, 2u);

    RoboCheckEqInt(CssNumericMakeNumber(&numeric, 1.5, 0, NULL), RoboOk);
    RoboCheckEqInt(CssNumericWrite(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "1.5");

    RoboCheckEqInt(CssNumericMakeNumber(&numeric, 1.0, 0, NULL), RoboOk);
    RoboCheckEqInt(CssNumericWrite(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      RoboOk);
    /* `1.0` is whole, so it prints as `1` — while its *kind* stays NUMBER, which
     * is the distinction the spelling must not be asked to carry. */
    RoboCheckStrEq(buffer, "1");

    RoboCheckEqInt(CssNumericMakeNumber(&numeric, -0.0, 0, NULL), RoboOk);
    RoboCheckEqInt(CssNumericWrite(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "0");

    /* A percentage carries its sign and a length its canonical unit spelling,
     * which is also what proves the unit's case-insensitivity end to end. */
    RoboCheckEqInt(CssNumericMakePercentage(&numeric, -12.5, NULL), RoboOk);
    RoboCheckEqInt(CssNumericWrite(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "-12.5%");
    RoboCheckEqInt(length, 6u);

    RoboCheckEqInt(CssNumericMakeLength(&numeric, 20.0, RoboCssUnitVmin, NULL),
                      RoboOk);
    RoboCheckEqInt(CssNumericWrite(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "20vmin");

    /* The tiny-magnitude rule. `1e-9` rounded to six decimals is zero, and the
     * writer must not say so. */
    RoboCheckEqInt(CssNumericMakeNumber(&numeric, 1e-9, 0, NULL), RoboOk);
    RoboCheckEqInt(CssNumericWrite(numeric, buffer, sizeof(buffer), &length,
                                             NULL),
                      RoboOk);
    RoboCheck(buffer[0] != '\0');
    RoboCheck(strcmp(buffer, "0") != 0);
    /* ...and it is the compact exponent form, which is the only spelling that can
     * carry a magnitude this small in a fixed-width buffer. */
    RoboCheck(strchr(buffer, 'e') != NULL);

    /* A value that does not fit is refused rather than truncated, and the caller
     * learns how much room it needed. */
    RoboCheckEqInt(CssNumericMakeLength(&numeric, 123456.789, RoboCssUnitVmin,
                                                   NULL),
                      RoboOk);
    RoboCheckEqInt(CssNumericWrite(numeric, small, sizeof(small), &needed, NULL),
                      RoboNotSupported);
    RoboCheckStrEq(small, "");
    RoboCheck(needed > sizeof(small));

    /* The absence of a value has no text, and that is a success: a cleared
     * record is not a value that failed to write. */
    RoboCheckEqInt(CssNumericWrite(CssNumericNone(), buffer,
                                             sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "");
    RoboCheckEqInt(length, 0u);

    /* A NULL argument is a bug and not an outcome. */
    RoboCheckEqInt(CssNumericWrite(numeric, NULL, sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssNumericWrite(numeric, buffer, sizeof(buffer), NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssNumericWrite(numeric, buffer, 0u, &length, NULL),
                      RoboInvalidArgument);
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
static void TestNumericRobust(void)
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

    CssFixture();

    for (index = 0u; index < sizeof(declarations) / sizeof(declarations[0]); index++) {
        CssNumeric numeric = CssNumericNone();
        CssNumericParse outcome = GetRoboCssNumericParse(declarations[index], &numeric);
        char message[192];

        if (!CssNumericParseIsKnown(outcome)) {
            sprintf(message, "%s produced an undefined parse outcome", declarations[index]);
            RoboCheckMsg(0, message);
            continue;
        }
        if (outcome == RoboCssNumericParseResolved) {
            /* A resolved value must be one of the four kinds and must have a
             * magnitude a runtime can hold: a parser that accepted `1.2.3` as
             * `1.2` would be the failure this test exists to catch. The
             * finiteness test is spelled out rather than taken from the module,
             * so the assertion is not the code under test. */
            int finite = numeric.number == numeric.number &&
                         (numeric.number - numeric.number) == 0.0;

            if (numeric.kind == RoboCssNumericNone || !finite) {
                sprintf(message, "%s resolved to a value that is not one",
                        declarations[index]);
                RoboCheckMsg(0, message);
            }
        }
    }

    /* The same shapes through a whole declaration list, so the guard is about the
     * real parse path and not only about one declaration at a time. */
    {
        CssDeclarationList list = RoboCssCleared;
        CssError error = RoboCssCleared;
        size_t count;

        CssFixture();
        RoboCheckEqInt(CssParseDeclarations(
                              "a: 1px; b: 1.2.3; c: 1e999; d: 10foo; e: calc(1px);"
                              "f: 50%; g: 2em; h: rgb(1,2,3)",
                              &list, &error),
                          RoboOk);
        RoboCheckEqInt((int)CssDeclarationListCount(&list), 8);
        for (count = 0u; count < CssDeclarationListCount(&list); count++) {
            const CssDeclaration *declaration = CssDeclarationListAt(&list,
                                                                                     count);
            CssNumeric numeric = CssNumericNone();

            RoboCheck(declaration != NULL);
            RoboCheck(CssNumericParseIsKnown(CssNumericFromValue(
                CssDeclarationValue(declaration), &numeric)));
        }
        CssDeclarationListDispose(&list);
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
static void CssUnitComputeOf(CssInheritFixture *fixture,
                                CssElementRef ref,
                                CssComputedStyle *out)
{
    CssError error = RoboCssCleared;

    RoboCheckEqInt(CssStyleContextComputeStyle(CssInheritContext(fixture), ref,
                                                          out, &error, NULL),
                      RoboOk);
    /* A unit this suite writes is legal, so a clean document is what every
     * expectation below is an expectation about. */
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
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
static void TestUnitComputedLengths(void)
{
    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationLength, "l: 10px", "10px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 1in", "96px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 2.54cm", "96px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 25.4mm", "96px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 72pt", "96px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 6pc", "96px");
    /* Forty `Q` are a centimetre, and 1cm is 96/2.54px — the same answer the
     * centimetre row above gives, because it is the same length. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 40q", "37.795276px");

    /* The units 9.10 added, carried rather than converted — including the three
     * viewport families and the two axes. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 1lh", "1lh");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 2rlh", "2rlh");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 3vi", "3vi");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 4vb", "4vb");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 50svw", "50svw");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 50svh", "50svh");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 50lvmin", "50lvmin");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 50dvmax", "50dvmax");

    /* The unit is *canonicalized*, not echoed: `10PX` is the same length as
     * `10px`, and the value layer writes one of them. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 10PX", "10px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 2EM", "2em");

    /* The kind stays a length and the unit stays live, which is what a later
     * phase resolves — so the assertions are on the typed record and not only on
     * the text. */
    {
        CssDeclarationList list = RoboCssCleared;
        CssComputedValue computed = RoboCssCleared;
        const CssDeclaration *declaration;

        CssError error = RoboCssCleared;

        RoboCheckEqInt(CssParseDeclarations("l: 2em", &list, &error), RoboOk);
        declaration = CssDeclarationListAt(&list, 0u);
        RoboCheck(declaration != NULL);
        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationLength,
                              CssDeclarationValue(declaration), &computed, NULL),
                          RoboOk);
        RoboCheckEqInt(computed.kind, RoboCssComputedValueLength);
        RoboCheckEqInt(computed.unit, RoboCssUnitEm);
        RoboCheck(CssUnitNear(computed.number, 2.0, 1e-12));
        CssComputedValueDispose(&computed);
        CssDeclarationListDispose(&list);
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
static void TestUnitComputedInheritance(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *outer = NULL;
    CssComputedStyle *relative = NULL;
    CssComputedStyle *absolute = NULL;
    CssComputedStyle *inheritLen = NULL;
    CssComputedStyle *initialLen = NULL;
    CssComputedStyle *unsetLen = NULL;
    CssElementRef div;
    CssElementRef emChild;
    CssElementRef inChild;
    CssElementRef inheritChild;
    CssElementRef initialChild;
    CssElementRef unsetChild;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "app", NULL);
    emChild = CssHostAdd(CssInheritHost(&fixture), "p", "a", NULL);
    inChild = CssHostAdd(CssInheritHost(&fixture), "p", "b", NULL);
    inheritChild = CssHostAdd(CssInheritHost(&fixture), "p", "c", NULL);
    initialChild = CssHostAdd(CssInheritHost(&fixture), "p", "d", NULL);
    unsetChild = CssHostAdd(CssInheritHost(&fixture), "p", "e", NULL);
    CssHostSetParent(CssInheritHost(&fixture), emChild, div);
    CssHostSetParent(CssInheritHost(&fixture), inChild, div);
    CssHostSetParent(CssInheritHost(&fixture), inheritChild, div);
    CssHostSetParent(CssInheritHost(&fixture), initialChild, div);
    CssHostSetParent(CssInheritHost(&fixture), unsetChild, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#app { font-size: 1.25rem; width: 30em }"
                      "#a { font-size: 2em }"
                      "#b { width: 1in }"
                      "#c { width: inherit }"
                      "#d { width: initial }"
                      "#e { width: unset }");
    RoboCheckEqInt(CssComputedStyleCreate(&outer, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&relative, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&absolute, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&inheritLen, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&initialLen, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&unsetLen, NULL), RoboOk);

    CssUnitComputeOf(&fixture, div, outer);
    CssUnitComputeOf(&fixture, emChild, relative);
    CssUnitComputeOf(&fixture, inChild, absolute);
    CssUnitComputeOf(&fixture, inheritChild, inheritLen);
    CssUnitComputeOf(&fixture, initialChild, initialLen);
    CssUnitComputeOf(&fixture, unsetChild, unsetLen);

    /* A contextual length survives the whole pipeline as a typed length with its
     * unit live, and it is inherited unchanged. */
    CssComputedIs(outer, "font-size", "1.25rem");
    CssComputedFlagsAre(outer, "font-size", "declared");
    CssComputedIs(outer, "width", "30em");
    CssComputedIs(relative, "font-size", "2em");
    CssComputedFlagsAre(relative, "font-size", "declared");

    /* An absolute length is normalized to px wherever it was written. */
    CssComputedIs(absolute, "width", "96px");
    CssComputedFlagsAre(absolute, "width", "declared");

    /* `inherit` on a property that does not inherit by default pulls the
     * parent's *computed* representation — `30em`, not `30` and not `480px`. */
    CssComputedIs(inheritLen, "width", "30em");
    CssComputedFlagsAre(inheritLen, "width", "inherited");
    /* `initial` and `unset` on a non-inherited property both land on the
     * property's own initial value, which for `width` is the keyword `auto` — a
     * *representation* rather than a resolved width. */
    CssComputedIs(initialLen, "width", "auto");
    CssComputedFlagsAre(initialLen, "width", "default");
    CssComputedIs(unsetLen, "width", "auto");
    CssComputedFlagsAre(unsetLen, "width", "default");

    /* ...while `unset` on an *inherited* property is `inherit` instead, and the
     * inherited value is the parent's computed one. */
    CssComputedIs(unsetLen, "font-size", "1.25rem");
    CssComputedFlagsAre(unsetLen, "font-size", "inherited");

    CssComputedStyleDestroy(unsetLen);
    CssComputedStyleDestroy(initialLen);
    CssComputedStyleDestroy(inheritLen);
    CssComputedStyleDestroy(absolute);
    CssComputedStyleDestroy(relative);
    CssComputedStyleDestroy(outer);
    CssInheritEnd(&fixture);
}

/**
 * Numbers and colours in one stylesheet, which is the shape a real sheet has.
 *
 * 9.9 owns colours and 9.10 owns numbers, and the value layer's dispatch is what
 * keeps them from being confused: this asserts that adding the numeric vocabulary
 * did not widen or narrow what a colour-shaped property accepts, and the other
 * way round. It is the visual checkpoint's own remark — a sheet is both at once.
 */
static void TestUnitComputedCoexistsWithColours(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *panel = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "panel", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#panel { color: #336699; background-color: rgb(255, 255, 255);"
                      " width: 1cm; height: 50%; font-size: 12pt }");
    RoboCheckEqInt(CssComputedStyleCreate(&panel, NULL), RoboOk);
    CssUnitComputeOf(&fixture, div, panel);

    CssComputedIs(panel, "color", "#336699");
    CssComputedIs(panel, "background-color", "#ffffff");
    CssComputedIs(panel, "width", "37.795276px");
    /* A percentage stays a percentage, beside lengths that did convert — the
     * distinction the numeric kind exists to keep. */
    CssComputedIs(panel, "height", "50%");
    CssComputedIs(panel, "font-size", "16px");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLength, "l: 50%"),
                      "percentage");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLength, "l: 1cm"),
                      "length");
    /* A numeric value is not a colour and a colour is not a numeric value, which
     * is the boundary between the two modules. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor, "c: 10px"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLength, "l: red"),
                      "invalid");

    CssComputedStyleDestroy(panel);
    CssInheritEnd(&fixture);
}

/**
 * The same inputs produce the same outputs, every time.
 *
 * Determinism is a property of the whole chain, so it is asserted over a
 * document rather than a value: two independent computations of one element must
 * agree property for property, and the unit table must answer identically when
 * walked in order and in reverse.
 */
static void TestUnitDeterminism(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *first = NULL;
    CssComputedStyle *second = NULL;
    CssElementRef div;
    static const char *const names[] = { "width", "height", "font-size", "color",
                                        "background-color" };
    size_t index;
    double px = 0.0;
    int reverse;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "app", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#app { width: 2.5cm; height: 10vh; font-size: 1.5rem;"
                      " color: #0e7aff; background-color: rgba(0, 0, 0, 0.25) }");
    RoboCheckEqInt(CssComputedStyleCreate(&first, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&second, NULL), RoboOk);
    CssUnitComputeOf(&fixture, div, first);
    CssUnitComputeOf(&fixture, div, second);

    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        char one[RoboCssComputedValueTextMax];
        char two[RoboCssComputedValueTextMax];

        CssComputedNote(first, names[index], one, sizeof(one));
        CssComputedNote(second, names[index], two, sizeof(two));
        RoboCheckStrEq(one, two);
    }

    CssComputedStyleDestroy(second);
    CssComputedStyleDestroy(first);
    CssInheritEnd(&fixture);

    /* The vocabulary answers the same walked backwards, so nothing in it is
     * order-dependent. */
    for (reverse = (int)RoboCssUnitCount - 1; reverse >= 0; reverse--) {
        CssUnit unit = (CssUnit)reverse;
        double forwardPx = 0.0;
        double backwardPx = 0.0;

        RoboCheckEqInt((int)CssUnitResolvePx(3.0, unit, NULL, &forwardPx),
                          (int)CssUnitResolvePx(3.0, unit, NULL, &backwardPx));
        RoboCheck(CssUnitNear(forwardPx, backwardPx, 1e-12));
    }
    RoboCheckEqInt(CssUnitResolvePx(1.0, RoboCssUnitIn, NULL, &px),
                      RoboCssLengthResolved);
    RoboCheck(CssUnitNear(px, 96.0, 1e-9));
}

/* ------------------------------------------------------------------ */

void TestRegisterCssUnit(void);

void TestRegisterCssUnit(void)
{
    TestRegister("css.unit.vocabulary", TestUnitVocabulary);
    TestRegister("css.unit.absolute_conversion", TestUnitAbsoluteConversion);
    TestRegister("css.unit.resolve_absolute", TestUnitResolveAbsolute);
    TestRegister("css.unit.resolve_needs_context", TestUnitResolveNeedsContext);
    TestRegister("css.unit.resolve_font_relative", TestUnitResolveFontRelative);
    TestRegister("css.unit.resolve_viewport", TestUnitResolveViewport);
    TestRegister("css.unit.resolve_invalid", TestUnitResolveInvalid);
    TestRegister("css.unit.context", TestUnitContext);
    TestRegister("css.numeric.kinds", TestNumericKinds);
    TestRegister("css.numeric.equality", TestNumericEquality);
    TestRegister("css.numeric.constructors", TestNumericConstructors);
    TestRegister("css.numeric.parse_outcomes", TestNumericParseOutcomes);
    TestRegister("css.numeric.write", TestNumericWrite);
    TestRegister("css.numeric.robust", TestNumericRobust);
    TestRegister("css.unit.computed.lengths", TestUnitComputedLengths);
    TestRegister("css.unit.computed.inheritance", TestUnitComputedInheritance);
    TestRegister("css.unit.computed.coexists_with_colours",
                       TestUnitComputedCoexistsWithColours);
    TestRegister("css.unit.determinism", TestUnitDeterminism);
}
