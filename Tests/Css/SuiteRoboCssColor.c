/*
 * RoboCSS colour suite (Phase 9.9) — the model, the syntaxes and the integration.
 *
 * Three groups of tests, in the order the module is built:
 *
 *   the record    kinds, predicates, equality, and the cleared state
 *   the syntaxes  the named set (walked whole), all four hex lengths,
 *                 `rgb()`/`rgba()` and `hsl()`/`hsla()` in both argument
 *                 styles, `transparent`, `currentColor`, and the malformed
 *                 shapes that must not become colours
 *   integration   colours through the computed-value layer and the computed
 *                 style, including the two `currentColor` resolution rules
 *
 * The syntaxes are tested through *declaration text* rather than through
 * hand-built component values wherever a declaration can say the same thing,
 * because that is the path a stylesheet takes: tokenizer, grammar, declaration,
 * value tree, colour. A hand-built tree would prove the colour parser works on
 * trees the real parser never produces, which is the half of the job that cannot
 * break.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Reading a colour out of a declaration                               */
/* ------------------------------------------------------------------ */

/**
 * The specified value of the single declaration in @p text.
 *
 * @p list is filled and owns nothing here (declarations borrow the text), so the
 * caller must keep @p text alive for as long as it reads the value — which is why
 * every caller passes a literal.
 */
static const CssValue *CssColorValueOf(const char *text,
                                                 CssDeclarationList *list)
{
    CssError error = RoboCssCleared;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations(text, list, &error), RoboOk);
    /* One declaration out, which is the guard that matters: a fixture that
     * produced none would make every expectation about a NULL value. The error
     * *record* is deliberately not asserted on, because a colour value that is
     * malformed is usually a grammar-level error too (`rgb(` is an unclosed
     * function) and the subject of these tests is the colour parser's verdict on
     * a value the grammar did produce — not the grammar's. */
    RoboCheckEqInt((int)CssDeclarationListCount(list), 1);
    return CssDeclarationValue(CssDeclarationListAt(list, 0u));
}

/**
 * Parse the colour in @p declaration text, returning what came out.
 *
 * The colour record holds only integers, so it is safe to read after the list is
 * disposed — which is worth doing rather than leaking the list into every
 * assertion's scope.
 */
static CssColorParse GetRoboCssColorParse(const char *declaration,
                                             CssColor *out)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value = CssColorValueOf(declaration, &list);
    CssColorParse outcome;

    RoboCheck(value != NULL);
    outcome = CssColorFromValue(value, out);
    CssDeclarationListDispose(&list);
    return outcome;
}

/** Assert @p color writes as @p expected, and that the length agrees. */
static void CssColorWrittenIs(CssColor color, const char *expected)
{
    char text[RoboCssColorTextMax];
    size_t length = 0u;

    RoboCheckEqInt(CssColorWrite(color, text, sizeof(text), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(text, expected);
    RoboCheckEqInt(length, strlen(expected));
}

/**
 * Assert @p declaration is a colour and writes back as @p expected.
 *
 * The written form is the assertion and not the channels, deliberately: two
 * spellings of one colour compare equal *because* they serialize to the same
 * text, so a test that compared channels would pass while the two forms could
 * still disagree about alpha or about how they print.
 */
static void CssColorIs(const char *declaration, const char *expected)
{
    CssColor color = CssColorNone();

    RoboCheckEqInt((int)GetRoboCssColorParse(declaration, &color), RoboCssColorParseResolved);
    CssColorWrittenIs(color, expected);
}

/** Assert @p declaration's colour parses to @p outcome. */
static void CssColorOutcomeIs(const char *declaration, CssColorParse outcome)
{
    CssColor color = CssColorNone();

    RoboCheckEqInt((int)GetRoboCssColorParse(declaration, &color), (int)outcome);
}

/** Assert @p declaration is a colour whose channels are exactly as given. */
static void CssColorChannelsAre(const char *declaration, unsigned red, unsigned green,
                                   unsigned blue, unsigned alpha)
{
    CssColor color = CssColorNone();

    RoboCheckEqInt((int)GetRoboCssColorParse(declaration, &color), RoboCssColorParseResolved);
    RoboCheckEqInt(color.kind, RoboCssColorRgba);
    RoboCheckEqInt(color.red, red);
    RoboCheckEqInt(color.green, green);
    RoboCheckEqInt(color.blue, blue);
    RoboCheckEqInt(color.alpha, alpha);
}

/** Assert @p actual is exactly @p expected, naming @p label when they differ. */
static void CssColorEqualsMessage(const CssColor actual,
                                     const CssColor expected, const char *label)
{
    if (!CssColorEquals(actual, expected)) {
        TestFail(__FILE__, __LINE__, "%s: colours differ", label);
    }
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void TestColorRecords(void)
{
    CssColor none;
    CssColor invalid;
    CssColor current;
    CssColor red;
    CssColorKind kind;

    CssFixture();
    none = CssColorNone();
    invalid = CssColorInvalid();
    current = CssColorCurrent();
    red = CssColorMake(255u, 0u, 0u, 255u);

    /* Every kind is known and named, and an unknown one says so rather than
     * borrowing a real kind's name. */
    for (kind = RoboCssColorNone; kind < (CssColorKind)RoboCssColorKindCount;
         kind = (CssColorKind)(kind + 1)) {
        const char *name = CssColorKindName(kind);

        RoboCheck(CssColorKindIsKnown(kind));
        RoboCheck(name != NULL);
        RoboCheck(strcmp(name, "unknown") != 0);
    }
    RoboCheckEqInt(CssColorKindIsKnown((CssColorKind)99), 0);
    RoboCheckEqInt(CssColorKindIsKnown((CssColorKind)-1), 0);
    RoboCheckStrEq(CssColorKindName((CssColorKind)99), "unknown");
    RoboCheckStrEq(CssColorKindName(RoboCssColorRgba), "rgba");
    RoboCheckStrEq(CssColorKindName(RoboCssColorCurrent), "current");
    RoboCheckStrEq(CssColorKindName(RoboCssColorInvalid), "invalid");
    RoboCheckStrEq(CssColorKindName(RoboCssColorNone), "none");

    /* A record declared with the cleared initializer *is* NONE, which is what
     * makes `CssColor x = RoboCssCleared;` a usable starting point
     * rather than an uninitialized read. */
    {
        CssColor cleared = RoboCssCleared;

        RoboCheckEqInt(cleared.kind, RoboCssColorNone);
        RoboCheck(CssColorEquals(cleared, none));
    }

    RoboCheck(CssColorIsResolved(red));
    RoboCheck(!CssColorIsCurrent(red));
    RoboCheck(CssColorIsValid(red));
    RoboCheck(CssColorIsCurrent(current));
    RoboCheck(CssColorIsValid(current));
    RoboCheck(!CssColorIsResolved(current));
    RoboCheck(!CssColorIsCurrent(none));
    RoboCheck(!CssColorIsValid(none));
    RoboCheck(!CssColorIsValid(invalid));
    RoboCheck(!CssColorIsResolved(invalid));

    /* Equality is kind-aware, and that is the property that keeps a contextual
     * keyword from being mistaken for a channel set. */
    RoboCheck(CssColorEquals(red, CssColorMake(255u, 0u, 0u, 255u)));
    RoboCheck(!CssColorEquals(red, CssColorMake(255u, 0u, 0u, 254u)));
    RoboCheck(!CssColorEquals(red, CssColorMake(254u, 0u, 0u, 255u)));
    RoboCheck(!CssColorEquals(red, CssColorMake(255u, 1u, 0u, 255u)));
    RoboCheck(!CssColorEquals(red, CssColorMake(255u, 0u, 1u, 255u)));
    RoboCheck(!CssColorEquals(current, red));
    RoboCheck(!CssColorEquals(current, CssColorMake(0u, 0u, 0u, 0u)));
    RoboCheck(CssColorEquals(current, CssColorCurrent()));
    RoboCheck(CssColorEquals(none, CssColorNone()));
    RoboCheck(CssColorEquals(invalid, CssColorInvalid()));
    RoboCheck(!CssColorEquals(none, invalid));
    RoboCheck(!CssColorEquals(none, current));
}

static void TestColorParseOutcomes(void)
{
    CssColorParse outcome;

    CssFixture();
    for (outcome = RoboCssColorParseInvalid;
         outcome < (CssColorParse)RoboCssColorParseCount;
         outcome = (CssColorParse)(outcome + 1)) {
        const char *name = CssColorParseName(outcome);

        RoboCheck(CssColorParseIsKnown(outcome));
        RoboCheck(name != NULL);
        RoboCheck(strcmp(name, "unknown") != 0);
    }
    RoboCheckEqInt(CssColorParseIsKnown((CssColorParse)99), 0);
    RoboCheckStrEq(CssColorParseName((CssColorParse)99), "unknown");
    RoboCheckStrEq(CssColorParseName(RoboCssColorParseResolved), "resolved");
    RoboCheckStrEq(CssColorParseName(RoboCssColorParseCurrent), "current");
    RoboCheckStrEq(CssColorParseName(RoboCssColorParseDeferred), "deferred");
    RoboCheckStrEq(CssColorParseName(RoboCssColorParseMalformed), "malformed");
    RoboCheckStrEq(CssColorParseName(RoboCssColorParseUnknownName),
                      "unknown-name");
}

/* ------------------------------------------------------------------ */
/* The standard named set                                              */
/* ------------------------------------------------------------------ */

static void TestColorNamedRepresentative(void)
{
    CssColor color = CssColorNone();

    CssFixture();

    /* The CSS2.1 keywords, the SVG names, and the one CSS added afterwards. */
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("red"), &color), 1);
    CssColorWrittenIs(color, "#ff0000");
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("rebeccapurple"),
                                               &color),
                      1);
    CssColorWrittenIs(color, "#663399");
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("blanchedalmond"),
                                               &color),
                      1);
    CssColorWrittenIs(color, "#ffebcd");
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("lightslategrey"),
                                               &color),
                      1);
    CssColorWrittenIs(color, "#778899");

    /* Names are ASCII case-insensitive, because CSS keywords are. */
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("RED"), &color), 1);
    CssColorWrittenIs(color, "#ff0000");
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("ReBeCcApUrPlE"),
                                               &color),
                      1);
    CssColorWrittenIs(color, "#663399");

    /* The spellings that are two names for one colour disagree about nothing. */
    {
        static const char *const pairs[][2] = {
            { "gray", "grey" },
            { "aqua", "cyan" },
            { "fuchsia", "magenta" },
            { "darkslategray", "darkslategrey" },
            { "lightslategray", "lightslategrey" }
        };
        size_t index;

        for (index = 0u; index < sizeof(pairs) / sizeof(pairs[0]); index++) {
            CssColor first = CssColorNone();
            CssColor second = CssColorNone();

            RoboCheckEqInt(CssColorFromName(CssSliceFromText(pairs[index][0]),
                                                       &first),
                              1);
            RoboCheckEqInt(CssColorFromName(CssSliceFromText(pairs[index][1]),
                                                       &second),
                              1);
            RoboCheck(CssColorEquals(first, second));
        }
    }

    /* An unknown word is not a colour — and not black either, which is the
     * silent-default failure this return value exists to prevent. */
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("reddish"), &color), 0);
    RoboCheckEqInt(CssColorFromName(CssSliceFromText(""), &color), 0);
    RoboCheckEqInt(CssColorFromName(CssSliceEmpty(), &color), 0);
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("red"), NULL), 0);
    /* `transparent` is a colour but not a *named* one: it is answered where the
     * keyword is, so that a reader looking for it does not have to know it is
     * spelled as a table entry. */
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("transparent"),
                                               &color),
                      0);
    CssColorIs("c: transparent", "#00000000");
}

/**
 * The whole table, walked.
 *
 * The named set is 148 entries and the tests above touch five of them, so the
 * coverage that matters is here: every name resolves, every name resolves back to
 * the colour the table hands out by index, every colour is opaque, every spelling
 * is lowercase ASCII letters, and no spelling appears twice. The last two are the
 * checks that a table this size actually needs — a pasted display name
 * (`blanched-almond`) and a duplicated entry are both invisible to a
 * representative-name test and both make a colour unreachable or ambiguous.
 */
static void TestColorNamedTable(void)
{
    size_t count;
    size_t index;

    CssFixture();
    count = CssColorNamedCount();
    RoboCheckEqInt(count, 148u);
    RoboCheckEqInt(CssColorNamedAt(count, NULL, NULL), 0);
    RoboCheckEqInt(CssColorNamedAt((size_t)-1, NULL, NULL), 0);

    for (index = 0u; index < count; index++) {
        CssSlice name = CssSliceEmpty();
        CssColor byIndex = CssColorNone();
        CssColor byName = CssColorNone();
        size_t byte;
        size_t other;

        RoboCheckEqInt(CssColorNamedAt(index, &name, &byIndex), 1);
        RoboCheck(name.length > 0u);
        RoboCheck(CssColorIsResolved(byIndex));
        RoboCheckEqInt(byIndex.alpha, 255u);

        /* Present *and* reachable: the table hands it out and the lookup finds
         * the same colour, which is what makes the two doors one table. */
        RoboCheckEqInt(CssColorFromName(name, &byName), 1);
        RoboCheck(CssColorEquals(byIndex, byName));

        for (byte = 0u; byte < name.length; byte++) {
            uint8_t value = name.bytes[byte];

            RoboCheck(value >= (uint8_t)'a' && value <= (uint8_t)'z');
        }
        for (other = 0u; other < index; other++) {
            CssSlice earlier = CssSliceEmpty();

            RoboCheckEqInt(CssColorNamedAt(other, &earlier, NULL), 1);
            if (CssSliceEquals(earlier, name)) {
                TestFail(__FILE__, __LINE__, "duplicate named colour: %.*s",
                               (int)name.length, (const char *)name.bytes);
            }
        }
    }
    /* Both outputs are optional, and a caller that only wants the name gets it. */
    {
        CssSlice name = CssSliceEmpty();

        RoboCheckEqInt(CssColorNamedAt(0u, &name, NULL), 1);
        RoboCheck(name.length > 0u);
    }
}

/* ------------------------------------------------------------------ */
/* Hex                                                                 */
/* ------------------------------------------------------------------ */

static void TestColorHex(void)
{
    CssColor color = CssColorNone();

    CssFixture();

    /* `#rgb` doubles each nibble rather than zero-padding it: `#f00` is red, and
     * reading it as (0x0f, 0x00, 0x00) is the classic three-digit mistake. */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("f00"), &color), 1);
    CssColorWrittenIs(color, "#ff0000");
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("0f0"), &color), 1);
    CssColorWrittenIs(color, "#00ff00");
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("abc"), &color), 1);
    CssColorWrittenIs(color, "#aabbcc");

    /* `#rgba` carries alpha in the fourth nibble. */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("0f08"), &color), 1);
    CssColorWrittenIs(color, "#00ff0088");
    /* ...and an alpha nibble that is opaque normalises away: `#f00f` is `#ff0000`
     * exactly, so writing it back with a redundant `ff` would be the one place
     * this representation disagreed with the `#rrggbb` spelling of itself. */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("f00f"), &color), 1);
    CssColorWrittenIs(color, "#ff0000");
    RoboCheckEqInt(color.alpha, 255u);

    /* `#rrggbb` is opaque, `#rrggbbaa` is not. */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("336699"), &color), 1);
    CssColorWrittenIs(color, "#336699");
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("33669980"), &color), 1);
    CssColorWrittenIs(color, "#33669980");

    /* Hexadecimal digits are case-insensitive, and the *stored* colour is the
     * same one either way round. */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("AbCdEf80"), &color), 1);
    CssColorWrittenIs(color, "#abcdef80");
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("abcdef80"), &color), 1);
    CssColorWrittenIs(color, "#abcdef80");

    /* The four lengths and nothing else: short, long and almost-right are all
     * refused rather than padded or truncated. */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText(""), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("f"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("ff"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("fffff"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("fffffff"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("fffffffff"), &color),
                      0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("ggg"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("12g456"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("ff 000"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("0x1234"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceEmpty(), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("fff"), NULL), 0);

    /* Through the whole pipeline, a hash token is the same colour: the tokenizer
     * strips the `#` and the value accessors are what the parser reads. */
    CssColorIs("c: #f00", "#ff0000");
    CssColorIs("c: #F00", "#ff0000");
    CssColorIs("c: #336699", "#336699");
    CssColorIs("c: #33669980", "#33669980");
    CssColorIs("c: #0f08", "#00ff0088");

    /* A hash spelling that is not one of the four lengths is not a colour. */
    CssColorOutcomeIs("c: #ff", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: #ggg", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: #fffffff", RoboCssColorParseMalformed);
}

/* ------------------------------------------------------------------ */
/* rgb() / rgba()                                                      */
/* ------------------------------------------------------------------ */

static void TestColorRgb(void)
{
    CssFixture();

    /* Numeric channels, both spellings of the function. */
    CssColorIs("c: rgb(255, 0, 0)", "#ff0000");
    CssColorIs("c: rgba(255, 0, 0)", "#ff0000");
    CssColorIs("c: rgb(0, 128, 0)", "#008000");
    CssColorIs("c: rgb(51, 102, 153)", "#336699");

    /* Percentage channels, and the fact that 100% is 255 rather than 100. */
    CssColorIs("c: rgb(100%, 0%, 0%)", "#ff0000");
    CssColorIs("c: rgb(20%, 40%, 60%)", "#336699");
    CssColorIs("c: rgb(50%, 50%, 50%)", "#808080");
    /* 51% of 255 is 130.05, which lands on 130: the channel is rounded once, at
     * the end, rather than at each step of the conversion. */
    CssColorIs("c: rgb(51%, 40%, 60%)", "#826699");
    /* Mixed number and percentage channels are CSS, and are not mixed up. */
    CssColorIs("c: rgb(255, 0%, 0)", "#ff0000");

    /* Alpha: a number, a percentage, after the legacy comma and after the
     * modern slash. */
    CssColorIs("c: rgba(255, 0, 0, 0.5)", "#ff000080");
    CssColorIs("c: rgba(255, 0, 0, 0)", "#ff000000");
    CssColorIs("c: rgba(255, 0, 0, 1)", "#ff0000");
    CssColorIs("c: rgba(255, 0, 0, 50%)", "#ff000080");
    CssColorIs("c: rgb(255 0 0 / 50%)", "#ff000080");
    CssColorIs("c: rgb(255 0 0 / 0.25)", "#ff000040");

    /* The modern space syntax, and whitespace everywhere it is legal. */
    CssColorIs("c: rgb(255 0 0)", "#ff0000");
    CssColorIs("c: rgb( 255   0   0 )", "#ff0000");
    CssColorIs("c: rgb( 255 , 0 , 0 )", "#ff0000");
    /* Percentage channels in the space syntax too, and 50% is 128 rather than
     * 127: the channel is rounded to nearest, not truncated. */
    CssColorIs("c: rgb(0% 50% 100%)", "#0080ff");

    /* Channels clamp rather than wrap: 300 is red, not a colour with a wrapped
     * green channel, and a negative channel is zero. */
    CssColorChannelsAre("c: rgb(300, 0, 0)", 255u, 0u, 0u, 255u);
    CssColorChannelsAre("c: rgb(-5, 0, 0)", 0u, 0u, 0u, 255u);
    CssColorChannelsAre("c: rgb(200%, 0%, 0%)", 255u, 0u, 0u, 255u);
    CssColorChannelsAre("c: rgba(255, 0, 0, 2)", 255u, 0u, 0u, 255u);
    CssColorChannelsAre("c: rgba(255, 0, 0, -1)", 255u, 0u, 0u, 0u);

    /* Fractional channels round to the nearest 8-bit step, once, at the end. */
    CssColorChannelsAre("c: rgb(0.5, 1.5, 2.5)", 1u, 2u, 3u, 255u);
    CssColorChannelsAre("c: rgba(0, 0, 0, 0.5)", 0u, 0u, 0u, 128u);

    /* Both spellings of the function accept both shapes, which is CSS Color 4's
     * rule and the reason `rgb` and `rgba` are not distinguished here. */
    CssColorIs("c: rgb(255, 0, 0, 0.5)", "#ff000080");
    CssColorIs("c: rgba(255 0 0)", "#ff0000");

    /* Not a colour: nothing, too few, too many, the wrong separator, a
     * dimension where a channel belongs, or a nested function. */
    CssColorOutcomeIs("c: rgb()", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(1)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(1, 2)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(1, 2, 3, 4, 5)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(1 2 3 4)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(255, 0 0)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(1, 2, 3 / 4)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(10px, 0, 0)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(255, 0)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(red, 0, 0)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(1e999, 0, 0)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: rgb(currentColor, 0, 0)", RoboCssColorParseMalformed);

    /* A hand-built call — no source, no children — is refused rather than read
     * past its end. */
    {
        CssValue function = RoboCssCleared;
        CssColor parsed = CssColorNone();

        RoboCheckEqInt(CssValueMakeFunction(&function,
                                                       CssSliceFromText("rgb"),
                                                       CssRangeAt(0u), NULL),
                          RoboOk);
        RoboCheckEqInt(CssColorFromRgbFunction(&function, &parsed), 0);
        RoboCheckEqInt(CssColorFromFunction(&function, &parsed),
                          RoboCssColorParseMalformed);
        CssValueDispose(&function);
        RoboCheckEqInt(CssColorFromRgbFunction(NULL, &parsed), 0);
        RoboCheckEqInt((int)CssColorFromFunction(NULL, &parsed),
                          (int)RoboCssColorParseInvalid);
    }
}

/* ------------------------------------------------------------------ */
/* hsl() / hsla()                                                      */
/* ------------------------------------------------------------------ */

/** Assert two declarations produce the *same* colour, whatever it is. */
static void CssColorSame(const char *left, const char *right)
{
    CssColor first = CssColorNone();
    CssColor second = CssColorNone();

    RoboCheckEqInt((int)GetRoboCssColorParse(left, &first), (int)RoboCssColorParseResolved);
    RoboCheckEqInt((int)GetRoboCssColorParse(right, &second), (int)RoboCssColorParseResolved);
    if (!CssColorEquals(first, second)) {
        char firstText[RoboCssColorTextMax];
        char secondText[RoboCssColorTextMax];
        size_t length = 0u;

        /* Both writes are checked, because a failed write leaves the buffer
         * untouched and the message would then name stack noise as the two
         * colours — which is worse than no message. */
        firstText[0] = '\0';
        secondText[0] = '\0';
        if (CssColorWrite(first, firstText, sizeof(firstText), &length, NULL) !=
            RoboOk) {
            firstText[0] = '\0';
        }
        if (CssColorWrite(second, secondText, sizeof(secondText), &length,
                                 NULL) != RoboOk) {
            secondText[0] = '\0';
        }
        TestFail(__FILE__, __LINE__, "%s and %s differ: %s vs %s", left, right,
                       firstText, secondText);
    }
}

static void TestColorHsl(void)
{
    CssColor color = CssColorNone();

    CssFixture();

    /* The canonical cases, and they are canonical because they are the primary
     * colours: a conversion that is wrong by a sector lands somewhere else. */
    CssColorIs("c: hsl(0, 100%, 50%)", "#ff0000");
    CssColorIs("c: hsl(120, 100%, 25%)", "#008000");
    CssColorIs("c: hsl(240, 100%, 50%)", "#0000ff");
    CssColorIs("c: hsl(0, 0%, 0%)", "#000000");
    CssColorIs("c: hsl(0, 0%, 100%)", "#ffffff");
    CssColorIs("c: hsl(180, 100%, 50%)", "#00ffff");

    /* The conversion agrees with the named set and with the hex spelling of the
     * same colour: three syntaxes, one value. */
    RoboCheckEqInt((int)GetRoboCssColorParse("c: hsl(120, 100%, 25%)", &color),
                      (int)RoboCssColorParseResolved);
    {
        CssColor green = CssColorNone();

        RoboCheckEqInt(CssColorFromName(CssSliceFromText("green"),
                                                   &green),
                          1);
        RoboCheck(CssColorEquals(color, green));
    }
    CssColorSame("c: hsl(0, 100%, 50%)", "c: #ff0000");
    CssColorSame("c: hsl(180, 100%, 50%)", "c: #00ffff");
    /* 210° at full saturation, 20% lightness: the blue channel is lightness plus
     * half the chroma, so 102, and the green is the sector's intermediate — 51,
     * not 68. */
    CssColorSame("c: hsl(210, 100%, 20%)", "c: #003366");
    CssColorSame("c: hsl(210, 100%, 20%)", "c: rgb(0, 51, 102)");

    /* Alpha: the legacy fourth argument and the modern slash, numeric or
     * percentage, and `hsla` is the same function. */
    CssColorIs("c: hsla(0, 100%, 50%, 0.5)", "#ff000080");
    CssColorIs("c: hsla(0, 100%, 50%, 50%)", "#ff000080");
    CssColorIs("c: hsl(0 100% 50% / 50%)", "#ff000080");
    CssColorIs("c: hsl(0, 100%, 50%, 0)", "#ff000000");
    CssColorIs("c: hsl(0 100% 50%)", "#ff0000");

    /* The modern space syntax, and whitespace where it is legal. */
    CssColorIs("c: hsl( 0   100%   50% )", "#ff0000");
    CssColorIs("c: hsl( 0 , 100% , 50% )", "#ff0000");
    CssColorSame("c: hsl(120 50 50)", "c: hsl(120, 50%, 50%)");

    /* Hue is normalised, not clamped: a wheel can be written either way round,
     * and one and a half turns is half a turn. */
    CssColorSame("c: hsl(-120, 100%, 50%)", "c: hsl(240, 100%, 50%)");
    CssColorSame("c: hsl(-240, 100%, 50%)", "c: hsl(120, 100%, 50%)");
    CssColorSame("c: hsl(480, 100%, 50%)", "c: hsl(120, 100%, 50%)");
    CssColorSame("c: hsl(720, 100%, 50%)", "c: hsl(0, 100%, 50%)");
    CssColorSame("c: hsl(360, 100%, 50%)", "c: hsl(0, 100%, 50%)");
    CssColorSame("c: hsl(-360, 100%, 50%)", "c: hsl(0, 100%, 50%)");

    /* An angle is a number with one of the four angle units. */
    CssColorSame("c: hsl(0.5turn, 100%, 50%)", "c: hsl(180deg, 100%, 50%)");
    CssColorSame("c: hsl(200grad, 100%, 50%)", "c: hsl(180deg, 100%, 50%)");
    CssColorSame("c: hsl(3.141592653589793rad, 100%, 50%)", "c: hsl(180deg, 100%, 50%)");
    CssColorIs("c: hsl(0.5turn 100% 50%)", "#00ffff");
    CssColorSame("c: hsl(1.25turn, 100%, 50%)", "c: hsl(90, 100%, 50%)");

    /* Saturation and lightness clamp rather than wrapping, which is what makes a
     * negative saturation grey instead of a colour nobody asked for. */
    CssColorSame("c: hsl(0, 150%, 50%)", "c: hsl(0, 100%, 50%)");
    CssColorSame("c: hsl(0, -10%, 50%)", "c: hsl(0, 0%, 50%)");
    CssColorIs("c: hsl(0, 0%, 50%)", "#808080");
    CssColorSame("c: hsl(0, 100%, 150%)", "c: hsl(0, 100%, 100%)");
    CssColorSame("c: hsl(0, 100%, -10%)", "c: hsl(0, 100%, 0%)");

    /* An angle this large has no fractional part left in a double, and the bound
     * is documented and pinned here rather than left to the platform: the hue
     * saturates to zero instead of producing an artefact. */
    CssColorSame("c: hsl(1e300turn, 100%, 50%)", "c: hsl(0, 100%, 50%)");

    /* Malformed: nothing, too few, too many, a unit where a saturation belongs,
     * the wrong separator, a length or a percentage used as a hue, and the
     * legacy comma form written with bare numbers (CSS requires percentages
     * there, and the modern form is where numbers are read as percentages). */
    CssColorOutcomeIs("c: hsl()", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(1)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(0, 100%)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(0 100%)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(0, 100%, 50%, 0.5, 2)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(0, 100%, 50% / 0.5)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(0, 50, 50)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(10px 50% 50%)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(0, 10px, 50%)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(10% 50% 50%)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(0 100% 50% 0.5)", RoboCssColorParseMalformed);
    CssColorOutcomeIs("c: hsl(1e999, 100%, 50%)", RoboCssColorParseMalformed);
}

/* ------------------------------------------------------------------ */
/* currentColor, and the colour spaces this build defers to            */
/* ------------------------------------------------------------------ */

static void TestColorCurrentColor(void)
{
    CssColor color = CssColorNone();
    CssColor resolved = CssColorNone();

    CssFixture();

    /* Typed, not resolved: the keyword keeps its spelling and the record says it
     * is contextual rather than pretending to be a channel set. */
    CssColorOutcomeIs("c: currentColor", RoboCssColorParseCurrent);
    RoboCheckEqInt((int)GetRoboCssColorParse("c: currentColor", &color),
                      (int)RoboCssColorParseCurrent);
    RoboCheck(CssColorIsCurrent(color));
    RoboCheck(!CssColorIsResolved(color));
    RoboCheck(CssColorIsValid(color));
    RoboCheckEqInt(color.kind, RoboCssColorCurrent);
    CssColorWrittenIs(color, "currentColor");

    /* One keyword, so its spelling is not part of the value. */
    CssColorOutcomeIs("c: CURRENTCOLOR", RoboCssColorParseCurrent);
    CssColorOutcomeIs("c: currentcolor", RoboCssColorParseCurrent);
    {
        CssColor upper = CssColorNone();

        RoboCheckEqInt((int)GetRoboCssColorParse("c: CURRENTCOLOR", &upper),
                          (int)RoboCssColorParseCurrent);
        RoboCheck(CssColorEquals(upper, color));
    }

    /* The substitution itself, and the four cases. */
    RoboCheckEqInt(CssColorResolveCurrent(
                          CssColorMake(1u, 2u, 3u, 255u),
                          CssColorCurrent(), &resolved),
                      1);
    CssColorWrittenIs(resolved, "#010203");
    RoboCheckEqInt(CssColorResolveCurrent(
                          CssColorCurrent(), CssColorMake(0x33u, 0x66u, 0x99u, 0x80u),
                          &resolved),
                      1);
    CssColorWrittenIs(resolved, "#33669980");
    /* Nothing to substitute: staying contextual is the answer, and inventing
     * black would be the mistake the keyword exists to prevent. */
    RoboCheckEqInt(CssColorResolveCurrent(CssColorCurrent(),
                                                     CssColorCurrent(), &resolved),
                      0);
    RoboCheck(CssColorIsCurrent(resolved));
    RoboCheckEqInt(CssColorResolveCurrent(CssColorCurrent(),
                                                     CssColorNone(), &resolved),
                      0);
    RoboCheck(CssColorIsCurrent(resolved));
    /* A non-colour is not a colour, and resolving does not make it one. */
    RoboCheckEqInt(CssColorResolveCurrent(CssColorInvalid(),
                                                     CssColorMake(1u, 2u, 3u, 255u),
                                                     &resolved),
                      0);
    RoboCheckEqInt(resolved.kind, RoboCssColorInvalid);
    RoboCheckEqInt(CssColorResolveCurrent(CssColorNone(),
                                                     CssColorMake(1u, 2u, 3u, 255u),
                                                     &resolved),
                      0);
    RoboCheckEqInt(resolved.kind, RoboCssColorNone);
    /* The output is optional: the verdict is the return value. */
    RoboCheckEqInt(CssColorResolveCurrent(CssColorCurrent(),
                                                     CssColorMake(1u, 2u, 3u, 255u),
                                                     NULL),
                      1);
    RoboCheckEqInt(CssColorResolveCurrent(CssColorCurrent(),
                                                     CssColorCurrent(), NULL),
                      0);
}

/**
 * The colour functions this build recognises and cannot represent.
 *
 * The distinction being tested is the one that keeps a legal declaration from
 * being called invalid: `oklch(...)` is CSS this build has no representation for,
 * so it is reported as deferred and carried; `sparkle(1)` is not a colour
 * function at all, so it is refused.
 */
static void TestColorDeferredSpaces(void)
{
    static const char *const deferred[] = {
        "oklch(0.7 0.1 200)", "oklab(0.7 0.1 0.1)", "lab(50% 40 59.5)",
        "lch(50% 70 40)", "hwb(194 0% 0%)", "color(display-p3 1 0 0)",
        "color-mix(in srgb, red, blue)", "device-cmyk(0 0 0 1)"
    };
    static const char *const unrepresentable[] = {
        "hwb", "lab", "lch", "oklab", "oklch", "color", "color-mix", "device-cmyk"
    };
    size_t index;

    CssFixture();
    for (index = 0u; index < sizeof(deferred) / sizeof(deferred[0]); index++) {
        char declaration[64];

        (void)snprintf(declaration, sizeof(declaration), "c: %s", deferred[index]);
        CssColorOutcomeIs(declaration, RoboCssColorParseDeferred);
    }
    for (index = 0u; index < sizeof(unrepresentable) / sizeof(unrepresentable[0]);
         index++) {
        RoboCheckEqInt(CssColorFunctionIsUnrepresentable(
                              CssSliceFromText(unrepresentable[index])),
                          1);
        RoboCheckEqInt(CssColorFunctionIsUnrepresentable(
                              CssSliceFromText("rgb")),
                          0);
    }
    /* Case-insensitive, as function names are. */
    RoboCheckEqInt(CssColorFunctionIsUnrepresentable(
                          CssSliceFromText("OKLCH")),
                      1);
    RoboCheckEqInt(CssColorFunctionIsUnrepresentable(CssSliceEmpty()), 0);
    RoboCheckEqInt(CssColorFunctionIsUnrepresentable(
                          CssSliceFromText("")),
                      0);

    /* A function nobody has heard of is not a colour, and neither is an
     * identifier that is not a colour keyword. */
    CssColorOutcomeIs("c: sparkle(1)", RoboCssColorParseInvalid);
    CssColorOutcomeIs("c: nonsense", RoboCssColorParseUnknownName);
    CssColorOutcomeIs("c: reddish", RoboCssColorParseUnknownName);
}

/* ------------------------------------------------------------------ */
/* Shapes that are not colours                                        */
/* ------------------------------------------------------------------ */

static void TestColorNotAColour(void)
{
    CssColor color = CssColorNone();

    CssFixture();

    /* Two colours is not a colour, and neither is a number, a string or a
     * dimension — each of them is its own kind of wrong. */
    CssColorOutcomeIs("c: red blue", RoboCssColorParseInvalid);
    CssColorOutcomeIs("c: 42", RoboCssColorParseInvalid);
    CssColorOutcomeIs("c: 10px", RoboCssColorParseInvalid);
    CssColorOutcomeIs("c: \"red\"", RoboCssColorParseInvalid);
    CssColorOutcomeIs("c: #ff0000 #00ff00", RoboCssColorParseInvalid);
    CssColorOutcomeIs("c: none", RoboCssColorParseUnknownName);
    /* `transparent` is a colour; the keyword is answered where the ident is. */
    CssColorOutcomeIs("c: transparent", RoboCssColorParseResolved);

    /* A value with nothing in it is not a colour, and neither is a NULL output:
     * both are answered rather than dereferenced. */
    {
        CssValue empty = RoboCssCleared;

        RoboCheckEqInt((int)CssColorFromValue(&empty, &color),
                          (int)RoboCssColorParseInvalid);
        RoboCheckEqInt((int)CssColorFromValue(NULL, &color),
                          (int)RoboCssColorParseInvalid);
        RoboCheckEqInt((int)CssColorFromValue(&empty, NULL),
                          (int)RoboCssColorParseInvalid);
    }
}

/**
 * Malformed colour input, at volume.
 *
 * Each string is either a token sequence the grammar can produce or one it
 * refuses, and none of them may crash, leak, or produce anything but a defined
 * outcome. The outcome is checked against the closed set rather than for a
 * particular value, because which of them is right for `#` is a judgement and
 * "something defined" is not.
 */
static void TestColorRobust(void)
{
    static const char *const declarations[] = {
        "c: #", "c: ##", "c: #fffffffffffff", "c: #0x1234", "c: #-1",
        "c: rgb(", "c: rgb()", "c: rgb(,,,,)", "c: rgb(/ / /)", "c: rgb(1 2 3 4 5)",
        "c: rgba(1,2,3,4,5)", "c: rgb(-1e400, 0, 0)", "c: rgb(1e-400, 0, 0)",
        "c: hsl(", "c: hsl()", "c: hsl(,,)", "c: hsl(0 0 0 /)", "c: hsl(1e999 50% 50%)",
        "c: currentColor()", "c: currentColor(", "c: currentcolor currentcolor",
        "c: transparent transparent", "c: ,,,,", "c: ;;;", "c: ()", "c: []", "c: {}",
        "c: inherit", "c: initial", "c: unset", "c: revert", "c: var(--x)",
        "c: calc(1px + 1px)", "c: url(x)", "c: \\", "c: 1 2 3", "c: red, blue"
    };
    size_t index;

    CssFixture();
    for (index = 0u; index < sizeof(declarations) / sizeof(declarations[0]); index++) {
        CssColor color = CssColorNone();
        CssColorParse outcome = GetRoboCssColorParse(declarations[index], &color);

        RoboCheck(CssColorParseIsKnown(outcome));
        RoboCheck(CssColorKindIsKnown(color.kind));
        /* Resolved means a real colour: never a half-filled record. */
        if (outcome == RoboCssColorParseResolved) {
            RoboCheck(CssColorIsResolved(color));
        }
        /* And a colour that was not produced is NONE or INVALID, never a
         * plausible-looking channel set nobody wrote. */
        if (outcome != RoboCssColorParseResolved &&
            outcome != RoboCssColorParseCurrent) {
            RoboCheck(!CssColorIsValid(color));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Serialization                                                       */
/* ------------------------------------------------------------------ */

static void TestColorWrite(void)
{
    char buffer[RoboCssColorTextMax];
    size_t length = 123u;
    CssDiag diag;

    CssFixture();

    /* The four kinds, one spelling each. NONE and INVALID are not CSS and are
     * written as the diagnostics they are: this is a diagnostic rendering, not a
     * CSSOM serialization. */
    CssColorWrittenIs(CssColorNone(), "none");
    CssColorWrittenIs(CssColorInvalid(), "invalid");
    CssColorWrittenIs(CssColorCurrent(), "currentColor");
    CssColorWrittenIs(CssColorMake(0u, 0u, 0u, 255u), "#000000");
    CssColorWrittenIs(CssColorMake(255u, 255u, 255u, 255u), "#ffffff");
    /* Alpha appears only when it is not opaque, and lowercase hex either way. */
    CssColorWrittenIs(CssColorMake(0xABu, 0xCDu, 0xEFu, 255u), "#abcdef");
    CssColorWrittenIs(CssColorMake(0xABu, 0xCDu, 0xEFu, 0x80u), "#abcdef80");
    CssColorWrittenIs(CssColorMake(0u, 0u, 0u, 0u), "#00000000");

    /* A NULL argument, an unknown kind and too little room are all reported
     * rather than written past. */
    CssDiagReset(&diag);
    RoboCheckEqInt(CssColorWrite(CssColorCurrent(), NULL, 8u, &length,
                                           &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.code, RoboInvalidArgument);
    RoboCheckEqInt(CssColorWrite(CssColorCurrent(), buffer, sizeof(buffer),
                                           NULL, &diag),
                      RoboInvalidArgument);
    {
        CssColor cleared = RoboCssCleared;

        RoboCheckEqInt(CssColorWrite(cleared, buffer, sizeof(buffer), &length,
                                               &diag),
                          RoboOk);
        RoboCheckStrEq(buffer, "none");
    }
    {
        CssColor unknown = CssColorMake(1u, 2u, 3u, 255u);

        unknown.kind = (CssColorKind)99;
        length = 0u;
        RoboCheckEqInt(CssColorWrite(unknown, buffer, sizeof(buffer), &length,
                                               &diag),
                          RoboInvalidArgument);
        RoboCheckEqInt(length, 0u);
        RoboCheckEqInt(buffer[0], '\0');
    }
    /* A LONGER spelling than the buffer holds is refused, and the caller is told
     * how much room it would have needed — no truncation, and no half a colour. */
    length = 0u;
    RoboCheckEqInt(CssColorWrite(CssColorMake(1u, 2u, 3u, 0x80u), buffer,
                                           5u, &length, &diag),
                      RoboNotSupported);
    RoboCheckEqInt(length, 9u);
    RoboCheckEqInt(buffer[0], '\0');
}

/* ------------------------------------------------------------------ */
/* Integration: colours through the pipeline                           */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style through the real context door, asserting success. */
static void CssColorComputeOf(CssInheritFixture *fixture,
                                 CssElementRef ref,
                                 CssComputedStyle *out)
{
    CssError error = RoboCssCleared;

    RoboCheckEqInt(CssStyleContextComputeStyle(CssInheritContext(fixture), ref,
                                                          out, &error, NULL),
                      RoboOk);
    /* A colour declaration this suite writes is legal, so a clean document is
     * what every expectation below is an expectation about. */
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
}

/** The kind of @p name's computed value, or NONE when the entry is absent. */
static CssComputedValueKind CssColorComputedKind(
    const CssComputedStyle *style, const char *name)
{
    const CssComputedValue *value =
        CssComputedStyleComputedByName(style, CssSliceFromText(name));

    return value ? value->kind : RoboCssComputedValueNone;
}

/**
 * Every ordinary spelling, computed.
 *
 * One sheet, one element per form, and the assertion is the *computed* text — so
 * what this proves is that the four syntaxes converge on one representation
 * rather than that each parser can be made to return something.
 */
static void TestColorComputedDeclared(void)
{
    static const struct {
        const char *id;
        const char *declared;
        const char *computed;
    } forms[] = {
        { "f0", "red", "#ff0000" },
        { "f1", "#f00", "#ff0000" },
        { "f2", "#FF0000", "#ff0000" },
        { "f3", "rgb(255, 0, 0)", "#ff0000" },
        { "f4", "rgb(255 0 0)", "#ff0000" },
        { "f5", "rgba(255, 0, 0, 1)", "#ff0000" },
        { "f6", "hsl(0, 100%, 50%)", "#ff0000" },
        { "f7", "hsl(0deg 100% 50%)", "#ff0000" },
        { "f8", "transparent", "#00000000" },
        { "f9", "#f008", "#ff000088" },
        { "fa", "rebeccapurple", "#663399" },
        { "fb", "rgb(51, 102, 153)", "#336699" }
    };
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef element[sizeof(forms) / sizeof(forms[0])];
    char sheet[1024];
    size_t offset = 0u;
    size_t index;

    CssFixture();
    CssInheritBegin(&fixture);
    sheet[0] = '\0';
    for (index = 0u; index < sizeof(forms) / sizeof(forms[0]); index++) {
        element[index] = CssHostAdd(CssInheritHost(&fixture), "div", forms[index].id,
                                      NULL);
        offset += (size_t)snprintf(sheet + offset, sizeof(sheet) - offset, "#%s { color: %s }",
                                   forms[index].id, forms[index].declared);
    }
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, sheet);
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    for (index = 0u; index < sizeof(forms) / sizeof(forms[0]); index++) {
        CssColorComputeOf(&fixture, element[index], style);
        CssComputedIs(style, "color", forms[index].computed);
        CssComputedFlagsAre(style, "color", "declared");
        RoboCheckEqInt(CssColorComputedKind(style, "color"),
                          RoboCssComputedValueColor);
    }

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/**
 * A colour that inherits, and one that is overridden on the way down.
 *
 * `background-color` is here because it is the one other property whose value is
 * a colour: it must NOT inherit, and a test that only looked at `color` would
 * pass whether or not the distinction existed.
 */
static void TestColorComputedInheritance(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *outer = NULL;
    CssComputedStyle *middle = NULL;
    CssComputedStyle *inner = NULL;
    CssElementRef div;
    CssElementRef section;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "app", NULL);
    section = CssHostAdd(CssInheritHost(&fixture), "section", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), section, div);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, section);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#app { color: #336699; background-color: #00ff00 }"
                      "section { color: rgba(255, 0, 0, 0.5) }");
    RoboCheckEqInt(CssComputedStyleCreate(&outer, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&middle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&inner, NULL), RoboOk);

    CssColorComputeOf(&fixture, div, outer);
    CssColorComputeOf(&fixture, section, middle);
    CssColorComputeOf(&fixture, paragraph, inner);

    CssComputedIs(outer, "color", "#336699");
    CssComputedFlagsAre(outer, "color", "declared");
    /* An inherited colour arrives as the parent's *typed* colour, not as a
     * re-parsed spelling: the two are distinguishable because `rgba(..., 0.5)`
     * is 128/255 and only the pipeline that computed it knows that. */
    CssComputedIs(middle, "color", "#ff000080");
    CssComputedFlagsAre(middle, "color", "declared");
    RoboCheckEqInt(CssColorComputedKind(middle, "color"),
                      RoboCssComputedValueColor);

    /* The paragraph declared nothing, so it inherits from its *immediate*
     * parent — the half-transparent red and not the grandparent's blue-ish. */
    CssComputedIs(inner, "color", "#ff000080");
    CssComputedFlagsAre(inner, "color", "inherited");
    RoboCheckEqInt(CssColorComputedKind(inner, "color"),
                      RoboCssComputedValueColor);

    /* `background-color` is declared on the outermost element and inherited by
     * nobody. */
    CssComputedIs(outer, "background-color", "#00ff00");
    CssComputedFlagsAre(outer, "background-color", "declared");
    CssComputedIs(inner, "background-color", "<absent>");

    CssComputedStyleDestroy(inner);
    CssComputedStyleDestroy(middle);
    CssComputedStyleDestroy(outer);
    CssInheritEnd(&fixture);
}

/**
 * The CSS-wide keywords, on a colour.
 *
 * `initial` on an inherited property is the interesting one: it must reset to
 * black rather than inherit red, and black is reachable only because the
 * registry says so (RoboCSSPropertyRegistry.c) — a layer that guessed would
 * guess wrong. `unset` splits by the property, which is the keyword's whole
 * point, and it is why both properties are here.
 */
static void TestColorComputedWideKeywords(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parent = NULL;
    CssComputedStyle *initialStyle = NULL;
    CssComputedStyle *inheritStyle = NULL;
    CssComputedStyle *unsetStyle = NULL;
    CssElementRef div;
    CssElementRef initialChild;
    CssElementRef inheritChild;
    CssElementRef unsetChild;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    initialChild = CssHostAdd(CssInheritHost(&fixture), "p", "a", NULL);
    inheritChild = CssHostAdd(CssInheritHost(&fixture), "p", "b", NULL);
    unsetChild = CssHostAdd(CssInheritHost(&fixture), "p", "c", NULL);
    CssHostSetParent(CssInheritHost(&fixture), initialChild, div);
    CssHostSetParent(CssInheritHost(&fixture), inheritChild, div);
    CssHostSetParent(CssInheritHost(&fixture), unsetChild, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: #336699; background-color: #00ff00 }"
                      "#a { color: initial; background-color: initial }"
                      "#b { color: inherit; background-color: inherit }"
                      "#c { color: unset; background-color: unset }");
    RoboCheckEqInt(CssComputedStyleCreate(&parent, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&initialStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&inheritStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&unsetStyle, NULL), RoboOk);

    CssColorComputeOf(&fixture, div, parent);
    CssColorComputeOf(&fixture, initialChild, initialStyle);
    CssColorComputeOf(&fixture, inheritChild, inheritStyle);
    CssColorComputeOf(&fixture, unsetChild, unsetStyle);

    /* The registry's initial colour for `color`, and still a typed colour. */
    CssComputedIs(initialStyle, "color", "#000000");
    CssComputedFlagsAre(initialStyle, "color", "default");
    RoboCheckEqInt(CssColorComputedKind(initialStyle, "color"),
                      RoboCssComputedValueColor);
    /* `background-color`'s initial value is `transparent`, which is also a
     * colour — and the empty one, so the alpha is the assertion. */
    CssComputedIs(initialStyle, "background-color", "#00000000");
    CssComputedFlagsAre(initialStyle, "background-color", "default");

    /* `inherit` pulls the parent's computed colour for either kind — including
     * the one that does not inherit by default, which is the whole reason the
     * keyword exists. */
    CssComputedIs(inheritStyle, "color", "#336699");
    CssComputedFlagsAre(inheritStyle, "color", "inherited");
    CssComputedIs(inheritStyle, "background-color", "#00ff00");
    CssComputedFlagsAre(inheritStyle, "background-color", "inherited");

    /* `unset` is `inherit` on `color` and `initial` on `background-color` — one
     * declaration, two different answers. */
    CssComputedIs(unsetStyle, "color", "#336699");
    CssComputedFlagsAre(unsetStyle, "color", "inherited");
    CssComputedIs(unsetStyle, "background-color", "#00000000");
    CssComputedFlagsAre(unsetStyle, "background-color", "default");

    CssComputedStyleDestroy(unsetStyle);
    CssComputedStyleDestroy(inheritStyle);
    CssComputedStyleDestroy(initialStyle);
    CssComputedStyleDestroy(parent);
    CssInheritEnd(&fixture);
}

/**
 * The two `currentColor` rules, and the case where neither can apply.
 *
 * `color: currentColor` is the colour the element would have *inherited*, and
 * `background-color: currentColor` is the element's *own* colour. The two
 * children below separate them: if the implementation used one rule for both,
 * exactly one of the four assertions would fail.
 */
static void TestColorComputedCurrentColorRules(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parent = NULL;
    CssComputedStyle *same = NULL;
    CssComputedStyle *own = NULL;
    CssComputedStyle *root = NULL;
    CssElementRef div;
    CssElementRef sameChild;
    CssElementRef ownChild;
    CssElementRef rootElement;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "app", NULL);
    sameChild = CssHostAdd(CssInheritHost(&fixture), "p", "a", NULL);
    ownChild = CssHostAdd(CssInheritHost(&fixture), "p", "b", NULL);
    rootElement = CssHostAdd(CssInheritHost(&fixture), "html", "r", NULL);
    CssHostSetParent(CssInheritHost(&fixture), sameChild, div);
    CssHostSetParent(CssInheritHost(&fixture), ownChild, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#app { color: #336699 }"
                      "#a { color: currentColor; background-color: currentColor }"
                      "#b { color: #ff0000; background-color: currentColor }"
                      "#r { color: currentColor; background-color: currentColor }");
    RoboCheckEqInt(CssComputedStyleCreate(&parent, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&same, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&own, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&root, NULL), RoboOk);

    CssColorComputeOf(&fixture, div, parent);
    CssColorComputeOf(&fixture, sameChild, same);
    CssColorComputeOf(&fixture, ownChild, own);
    CssColorComputeOf(&fixture, rootElement, root);

    /* Rule one: on `color`, `currentColor` is the parent's computed colour, so
     * the child ends up the same colour rather than a fixed value. */
    CssComputedIs(same, "color", "#336699");
    CssComputedFlagsAre(same, "color", "declared");
    RoboCheckEqInt(CssColorComputedKind(same, "color"),
                      RoboCssComputedValueColor);
    /* Rule two: everywhere else it is the element's own colour, which here is
     * the same one — so this alone would not distinguish the two rules. */
    CssComputedIs(same, "background-color", "#336699");
    RoboCheckEqInt(CssColorComputedKind(same, "background-color"),
                      RoboCssComputedValueColor);

    /* This is the pair that separates them: the element's own colour is red
     * while its parent's is not, and `background-color` must follow the element. */
    CssComputedIs(own, "color", "#ff0000");
    CssComputedIs(own, "background-color", "#ff0000");

    /* Nothing to resolve against: a root element has no parent computed style,
     * so `currentColor` stays contextual and stays flagged. Filling in black
     * would be exactly the mistake the keyword exists to prevent. */
    CssComputedIs(root, "color", "currentColor");
    CssComputedFlagsAre(root, "color", "declared+unresolved");
    RoboCheckEqInt(CssColorComputedKind(root, "color"),
                      RoboCssComputedValueColor);
    CssComputedIs(root, "background-color", "currentColor");
    CssComputedFlagsAre(root, "background-color", "declared+unresolved");

    CssComputedStyleDestroy(root);
    CssComputedStyleDestroy(own);
    CssComputedStyleDestroy(same);
    CssComputedStyleDestroy(parent);
    CssInheritEnd(&fixture);
}

/**
 * A reference, a misspelling, and a custom property — three different answers.
 *
 * `color: var(--brand)` is legal CSS that this build cannot resolve yet, so it is
 * carried rather than rejected; `background-color: potato` is a misspelling and is
 * reported invalid; `--brand` itself is never *interpreted*, so a colour written
 * into a custom property stays the tokens it was written as.
 */
static void TestColorComputedReferenceAndInvalid(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { --brand: #336699; color: var(--brand, red);"
                      " background-color: potato; display: block }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssColorComputeOf(&fixture, div, style);

    /* The reference is carried with its fallback intact, and is *not* a colour.
     * Nothing substitutes `--brand`, which is the promise custom properties are
     * held to. */
    CssComputedIs(style, "color", "var(--brand, red)");
    CssComputedFlagsAre(style, "color", "declared+unresolved");
    RoboCheckEqInt(CssColorComputedKind(style, "color"),
                      RoboCssComputedValuePreserved);

    /* The custom property keeps the tokens it was written as — a colour written
     * into one is not eagerly parsed into a colour. */
    CssComputedIs(style, "--brand", "#336699");
    CssComputedFlagsAre(style, "--brand", "declared+custom");
    RoboCheckEqInt(CssColorComputedKind(style, "--brand"),
                      RoboCssComputedValuePreserved);

    /* A misspelling is a different fact from a reference: the declaration was
     * there and was wrong, and a consumer can see which. */
    CssComputedFlagsAre(style, "background-color", "declared+invalid");
    RoboCheckEqInt(CssColorComputedKind(style, "background-color"),
                      RoboCssComputedValueInvalid);
    RoboCheckEqInt(CssComputedStyleHasInvalid(style), 1);
    /* ...and it does not corrupt its neighbours. */
    CssComputedIs(style, "display", "block");
    CssComputedFlagsAre(style, "display", "declared");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/**
 * The whole pipeline over a three-level tree.
 *
 * selector matching → specificity → cascade → inheritance → computed, with the
 * `color` on each level decided by a *different* kind of selector, so a break in
 * any one stage changes a colour that a later assertion reads.
 */
static void TestColorComputedSelectorPipeline(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *appStyle = NULL;
    CssComputedStyle *panelStyle = NULL;
    CssComputedStyle *titleStyle = NULL;
    CssComputedStyle *plainStyle = NULL;
    CssElementRef app;
    CssElementRef panel;
    CssElementRef title;
    CssElementRef plain;

    CssFixture();
    CssInheritBegin(&fixture);
    app = CssHostAdd(CssInheritHost(&fixture), "div", "app", NULL);
    panel = CssHostAdd(CssInheritHost(&fixture), "section", NULL, "panel");
    title = CssHostAdd(CssInheritHost(&fixture), "p", NULL, "title");
    plain = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), panel, app);
    CssHostSetParent(CssInheritHost(&fixture), title, panel);
    CssHostSetParent(CssInheritHost(&fixture), plain, panel);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#app { color: #336699 }"
                      ".panel { color: #00ff00 }"
                      ".title { color: #ff0000 }");
    RoboCheckEqInt(CssComputedStyleCreate(&appStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&panelStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&titleStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&plainStyle, NULL), RoboOk);

    CssColorComputeOf(&fixture, app, appStyle);
    CssColorComputeOf(&fixture, panel, panelStyle);
    CssColorComputeOf(&fixture, title, titleStyle);
    CssColorComputeOf(&fixture, plain, plainStyle);

    /* An id beats a class beats nothing, and each element's own declaration
     * reaches it as a typed colour. */
    CssComputedIs(appStyle, "color", "#336699");
    CssComputedFlagsAre(appStyle, "color", "declared");
    CssComputedIs(panelStyle, "color", "#00ff00");
    CssComputedFlagsAre(panelStyle, "color", "declared");
    CssComputedIs(titleStyle, "color", "#ff0000");
    CssComputedFlagsAre(titleStyle, "color", "declared");
    /* The one element nobody selected inherits its parent's computed colour. */
    CssComputedIs(plainStyle, "color", "#00ff00");
    CssComputedFlagsAre(plainStyle, "color", "inherited");

    CssComputedStyleDestroy(plainStyle);
    CssComputedStyleDestroy(titleStyle);
    CssComputedStyleDestroy(panelStyle);
    CssComputedStyleDestroy(appStyle);
    CssInheritEnd(&fixture);
}

/**
 * The same element computed twice produces the same colour, twice.
 *
 * Determinism is the property a later phase will depend on — a cached or
 * reordered style that spelled a colour differently the second time would make
 * every repaint a possible flicker — so it is asserted rather than assumed.
 */
static void TestColorComputedDeterminism(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *first = NULL;
    CssComputedStyle *second = NULL;
    CssElementRef div;
    char left[RoboCssColorTextMax];
    char right[RoboCssColorTextMax];
    size_t leftLength = 0u;
    size_t rightLength = 0u;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: hsl(210, 50%, 40%); background-color: #f008 }");
    RoboCheckEqInt(CssComputedStyleCreate(&first, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&second, NULL), RoboOk);

    CssColorComputeOf(&fixture, div, first);
    CssColorComputeOf(&fixture, div, second);

    /* Compared through the typed colour rather than through the text, because
     * the record is the thing that has to be stable. */
    {
        const CssComputedValue *a = CssComputedStyleComputedByName(
            first, CssSliceFromText("color"));
        const CssComputedValue *b = CssComputedStyleComputedByName(
            second, CssSliceFromText("color"));

        RoboCheck(a != NULL && b != NULL);
        if (a && b) {
            CssColorEqualsMessage(a->color, b->color, "color computed twice");
            RoboCheckEqInt(a->unresolved, b->unresolved);
        }
    }
    CssComputedIs(first, "color", "#336699");
    CssComputedIs(second, "color", "#336699");
    RoboCheckEqInt(CssColorWrite(
                          CssComputedStyleComputedByName(
                              first, CssSliceFromText("background-color"))->color,
                          left, sizeof(left), &leftLength, NULL),
                      RoboOk);
    RoboCheckEqInt(CssColorWrite(
                          CssComputedStyleComputedByName(
                              second, CssSliceFromText("background-color"))->color,
                          right, sizeof(right), &rightLength, NULL),
                      RoboOk);
    RoboCheckStrEq(left, right);
    RoboCheckEqInt(leftLength, rightLength);
    CssComputedIs(first, "background-color", "#ff000088");

    CssComputedStyleDestroy(second);
    CssComputedStyleDestroy(first);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */

void TestRegisterCssColor(void);

void TestRegisterCssColor(void)
{
    TestRegister("css.color.records", TestColorRecords);
    TestRegister("css.color.parse_outcomes", TestColorParseOutcomes);
    TestRegister("css.color.named_representative", TestColorNamedRepresentative);
    TestRegister("css.color.named_table", TestColorNamedTable);
    TestRegister("css.color.hex", TestColorHex);
    TestRegister("css.color.rgb", TestColorRgb);
    TestRegister("css.color.hsl", TestColorHsl);
    TestRegister("css.color.current_color", TestColorCurrentColor);
    TestRegister("css.color.deferred_spaces", TestColorDeferredSpaces);
    TestRegister("css.color.not_a_colour", TestColorNotAColour);
    TestRegister("css.color.robust", TestColorRobust);
    TestRegister("css.color.write", TestColorWrite);
    TestRegister("css.color.computed.declared", TestColorComputedDeclared);
    TestRegister("css.color.computed.inheritance", TestColorComputedInheritance);
    TestRegister("css.color.computed.wide_keywords", TestColorComputedWideKeywords);
    TestRegister("css.color.computed.current_color_rules",
                       TestColorComputedCurrentColorRules);
    TestRegister("css.color.computed.reference_and_invalid",
                       TestColorComputedReferenceAndInvalid);
    TestRegister("css.color.computed.selector_pipeline",
                       TestColorComputedSelectorPipeline);
    TestRegister("css.color.computed.determinism", TestColorComputedDeterminism);
}
