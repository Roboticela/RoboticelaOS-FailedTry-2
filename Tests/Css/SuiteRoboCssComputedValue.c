/*
 * RoboCSS computed values (Phase 9.8) — css.computedvalue.*
 *
 * The normaliser is the phase's whole claim: a declared value's *shape* decides
 * what it becomes, and what it becomes is a typed thing rather than a string.
 * Every test here is one shape, and the ones worth the most attention are the
 * ones where a wrong implementation still produces something plausible:
 *
 *   `#f00` read as a three-digit colour with the nibbles zero-padded rather
 *   than doubled is `#0f0000`, a plausible dark red that no one wrote.
 *
 *   `1in` left unconverted is a length whose unit is not the unit a computed
 *   value uses, so two equal lengths compare unequal.
 *
 *   `2em` converted to pixels would be a number that looks resolved and is
 *   wrong on every element with a different font size.
 *
 *   `rgb(300, 0, 0)` wrapped instead of clamped is not red.
 *
 *   `inherit` in a colour slot silently accepted as a colour name would resolve
 *   a property from a keyword — but the CSS-wide keywords never reach this
 *   function at all (9.7 obeys them), which is why the escape spelling is tested
 *   instead: `\72 ed` and `red` are the same colour.
 *
 * Fragility is tested as well as correctness: a malformed value must produce an
 * INVALID *answer*, never a crash and never a silent zero.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Units                                                              */
/* ------------------------------------------------------------------ */

static void TestCssComputedvalueUnits(void)
{
    CssFixture();

    RoboCheckEqInt(CssUnitIsKnown(RoboCssUnitPx), 1);
    RoboCheckEqInt(CssUnitIsKnown((CssUnit)RoboCssUnitCount), 0);
    RoboCheckEqInt(CssUnitIsKnown(RoboCssUnitNone), 1);

    RoboCheckStrEq(CssUnitName(RoboCssUnitPx), "px");
    RoboCheckStrEq(CssUnitName(RoboCssUnitRem), "rem");
    RoboCheckStrEq(CssUnitName(RoboCssUnitVmin), "vmin");
    /* A unit with no name is not a unit, and an empty name is the honest
     * rendering rather than a bogus one. */
    RoboCheckStrEq(CssUnitName(RoboCssUnitNone), "");
    RoboCheckStrEq(CssUnitName((CssUnit)99), "");

    /* Case-insensitive, because CSS spells units that way. */
    RoboCheckEqInt(CssUnitFromText("PX"), RoboCssUnitPx);
    RoboCheckEqInt(CssUnitFromText("Rem"), RoboCssUnitRem);
    RoboCheckEqInt(CssUnitFromText("pxx"), RoboCssUnitNone);
    RoboCheckEqInt(CssUnitFromText(""), RoboCssUnitNone);
    RoboCheckEqInt(CssUnitFromText(NULL), RoboCssUnitNone);
    /* `%` is not a unit: it is its own kind, because 50% of a width and 50% of a
     * line height are different computations. */
    RoboCheckEqInt(CssUnitFromText("%"), RoboCssUnitNone);

    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitPx), 1);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitIn), 1);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitPt), 1);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitEm), 0);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitVh), 0);
    RoboCheckEqInt(CssUnitIsAbsolute(RoboCssUnitNone), 0);

    /* CSS's own factors, and they are exact by definition: 1in is 96px, 1pt is
     * 4/3px, 1pc is 16px, 1cm is 96/2.54px. */
    RoboCheck(CssUnitToPx(1.0, RoboCssUnitPx) == 1.0);
    RoboCheck(CssUnitToPx(1.0, RoboCssUnitIn) == 96.0);
    RoboCheck(CssUnitToPx(1.0, RoboCssUnitPc) == 16.0);
    RoboCheck(CssUnitToPx(1.0, RoboCssUnitMm) > 3.77 &&
               CssUnitToPx(1.0, RoboCssUnitMm) < 3.78);
    /* A relative unit has no factor, so the value passes through rather than
     * being multiplied by something invented. */
    RoboCheck(CssUnitToPx(2.0, RoboCssUnitEm) == 2.0);
    RoboCheck(CssUnitToPx(7.0, RoboCssUnitNone) == 7.0);
}

/* ------------------------------------------------------------------ */
/* Colours                                                            */
/* ------------------------------------------------------------------ */

static void TestCssComputedvalueColorHex(void)
{
    CssColor color;

    CssFixture();

    /* Three digits: each nibble is *doubled*, which is the classic mistake —
     * `#f00` is (255, 0, 0) and not (15, 0, 0). */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("f00"), &color), 1);
    RoboCheckEqInt(color.red, 255);
    RoboCheckEqInt(color.green, 0);
    RoboCheckEqInt(color.blue, 0);
    RoboCheckEqInt(color.alpha, 255);

    /* Four: the fourth digit is alpha. */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("0f08"), &color), 1);
    RoboCheckEqInt(color.red, 0);
    RoboCheckEqInt(color.green, 255);
    RoboCheckEqInt(color.blue, 0);
    RoboCheckEqInt(color.alpha, 136);

    /* Six and eight, mixed case. */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("336699"), &color),
                      1);
    RoboCheckEqInt(color.red, 0x33);
    RoboCheckEqInt(color.green, 0x66);
    RoboCheckEqInt(color.blue, 0x99);
    RoboCheckEqInt(color.alpha, 255);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("AbCdEf80"), &color),
                      1);
    RoboCheckEqInt(color.red, 0xab);
    RoboCheckEqInt(color.green, 0xcd);
    RoboCheckEqInt(color.blue, 0xef);
    RoboCheckEqInt(color.alpha, 0x80);

    /* Every other length is refused rather than truncated: a three-digit colour
     * read as six would be a colour nobody wrote. */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText(""), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("f"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("ff"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("fffff"), &color),
                      0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("fffffff"), &color),
                      0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("fffffffff"), &color),
                      0);
    /* And a non-hex digit is not a digit. */
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("ggg"), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("12g456"), &color),
                      0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("ff 000"), &color),
                      0);
    RoboCheckEqInt(CssColorFromHex(CssSliceEmpty(), &color), 0);
    RoboCheckEqInt(CssColorFromHex(CssSliceFromText("fff"), NULL), 0);
}

static void TestCssComputedvalueColorNames(void)
{
    CssColor color;

    CssFixture();

    /* Representative names across the whole set, including the ones a partial
     * table would miss: a two-word name, an X11 oddity, and `rebeccapurple`. */
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("red"), &color),
                      1);
    RoboCheckEqInt(color.red, 255);
    RoboCheckEqInt(color.green, 0);
    RoboCheckEqInt(color.blue, 0);

    RoboCheckEqInt(
        CssColorFromName(CssSliceFromText("rebeccapurple"), &color), 1);
    RoboCheckEqInt(color.red, 0x66);
    RoboCheckEqInt(color.green, 0x33);
    RoboCheckEqInt(color.blue, 0x99);

    RoboCheckEqInt(
        CssColorFromName(CssSliceFromText("blanchedalmond"), &color), 1);
    RoboCheckEqInt(color.red, 0xff);
    RoboCheckEqInt(color.green, 0xeb);
    RoboCheckEqInt(color.blue, 0xcd);

    RoboCheckEqInt(
        CssColorFromName(CssSliceFromText("lightslategrey"), &color), 1);
    RoboCheckEqInt(color.red, 0x77);
    RoboCheckEqInt(color.blue, 0x99);

    /* The gray/grey pairs are the same colour under two spellings. */
    {
        CssColor g1;
        CssColor g2;

        RoboCheckEqInt(CssColorFromName(CssSliceFromText("gray"), &g1),
                          1);
        RoboCheckEqInt(CssColorFromName(CssSliceFromText("grey"), &g2),
                          1);
        RoboCheckEqInt(CssColorEquals(g1, g2), 1);
    }

    /* Case-insensitive. */
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("RED"), &color),
                      1);
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("ReBeCcApUrPlE"),
                                               &color),
                      1);

    /* `transparent` is a colour but not a named one: it is handled where the
     * keyword is, so a reader looking for it does not have to know it is spelled
     * as a table entry. */
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("transparent"),
                                               &color),
                      0);
    /* And an unknown word is not a colour rather than being silently black. */
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("reddish"), &color),
                      0);
    RoboCheckEqInt(CssColorFromName(CssSliceFromText(""), &color), 0);
    RoboCheckEqInt(CssColorFromName(CssSliceEmpty(), &color), 0);
    RoboCheckEqInt(CssColorFromName(CssSliceFromText("red"), NULL), 0);
}

/**
 * Every name in the standard set, through the public lookup.
 *
 * The point is coverage rather than the values: a table with a truncated or
 * misspelled entry would fail here, and a suite that checked only `red` would
 * not notice. The names are the same list CSS defines, spelled out because the
 * table itself is not readable from a test.
 */
static void TestCssComputedvalueColorNamedTable(void)
{
    static const char *const names[] = {
        "aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque",
        "black", "blanchedalmond", "blue", "blueviolet", "brown", "burlywood",
        "cadetblue", "chartreuse", "chocolate", "coral", "cornflowerblue", "cornsilk",
        "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", "darkgray",
        "darkgreen", "darkgrey", "darkkhaki", "darkmagenta", "darkolivegreen",
        "darkorange", "darkorchid", "darkred", "darksalmon", "darkseagreen",
        "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise",
        "darkviolet", "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue",
        "firebrick", "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite",
        "gold", "goldenrod", "gray", "green", "greenyellow", "grey", "honeydew",
        "hotpink", "indianred", "indigo", "ivory", "khaki", "lavender",
        "lavenderblush", "lawngreen", "lemonchiffon", "lightblue", "lightcoral",
        "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgreen", "lightgrey",
        "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray",
        "lightslategrey", "lightsteelblue", "lightyellow", "lime", "limegreen", "linen",
        "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid",
        "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen",
        "mediumturquoise", "mediumvioletred", "midnightblue", "mintcream", "mistyrose",
        "moccasin", "navajowhite", "navy", "oldlace", "olive", "olivedrab", "orange",
        "orangered", "orchid", "palegoldenrod", "palegreen", "paleturquoise",
        "palevioletred", "papayawhip", "peachpuff", "peru", "pink", "plum",
        "powderblue", "purple", "rebeccapurple", "red", "rosybrown", "royalblue",
        "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna",
        "silver", "skyblue", "slateblue", "slategray", "slategrey", "snow",
        "springgreen", "steelblue", "tan", "teal", "thistle", "tomato", "turquoise",
        "violet", "wheat", "white", "whitesmoke", "yellow", "yellowgreen"
    };
    CssColor color;
    size_t index;

    CssFixture();
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        RoboCheckEqInt(
            CssColorFromName(CssSliceFromText(names[index]), &color), 1);
        /* Every named colour is opaque; only `transparent` is not, and it is not
         * in this table. */
        RoboCheckEqInt(color.alpha, 255);
    }
    /* The count is the standard set's, spelled out: a silent deletion from the
     * table would otherwise be invisible. */
    RoboCheckEqInt((int)(sizeof(names) / sizeof(names[0])), 148);
}

static void TestCssComputedvalueColorRgbFunction(void)
{
    /* The legacy comma syntax, with numbers. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(255, 0, 0)", "#ff0000");
    /* And with the alpha, numeric and as a percentage. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgba(255, 0, 0, 0.5)",
                       "#ff000080");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgba(0, 0, 255, 50%)",
                       "#0000ff80");
    /* Three arguments to `rgba()` is legal and means opaque. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgba(0, 128, 0)",
                       "#008000");
    /* The modern space syntax, with and without a slash. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(255 0 0)", "#ff0000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(255 0 0 / 0.25)",
                       "#ff000040");
    /* Percentages for channels. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(100%, 0%, 0%)",
                       "#ff0000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(50% 50% 50%)",
                       "#808080");
    /* Case-insensitive function name, and whitespace everywhere. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c:  RGB( 1 , 2 , 3 ) ",
                       "#010203");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: RGBA(1 2 3 / 1)",
                       "#010203");

    /* Clamped, not wrapped: `300` is 255 and `-10` is 0, which is what CSS says
     * and what keeps a plausible-but-wrong channel from appearing. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(300, -10, 0)",
                       "#ff0000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(0 0 0 / 5)", "#000000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(0 0 0 / -1)",
                       "#00000000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(200%, 0%, 0%)",
                       "#ff0000");

    /* Malformed calls are INVALID answers rather than crashes or colours. The
     * message is the function's own: a caller reporting `rgb()` as "not a
     * colour" tells the author nothing they can act on. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(255, 0)",
                       "rgb() arguments are out of range or malformed");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(1, 2, 3, 4, 5)",
                       "rgb() arguments are out of range or malformed");
    /* Mixing the two separator styles mid-call is malformed, not "close
     * enough": the comma list ends at the first space-separated pair. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(255, 0 0)",
                       "rgb() arguments are out of range or malformed");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(1 2, 3)",
                       "rgb() arguments are out of range or malformed");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(1, 2, 3 / 4)",
                       "rgb() arguments are out of range or malformed");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(, 2, 3)",
                       "rgb() arguments are out of range or malformed");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(1, 2,)",
                       "rgb() arguments are out of range or malformed");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(/ 1 2 3)",
                       "rgb() arguments are out of range or malformed");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(red, 0, 0)",
                       "rgb() arguments are out of range or malformed");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb(1 2 3 4)",
                       "rgb() arguments are out of range or malformed");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb()",
                       "rgb() arguments are out of range or malformed");
    /* A space around a comma is decoration, not a second separator. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: rgb( 1 , 2 , 3 )",
                       "#010203");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor,
                                       "c: rgb(1, 2, 3)"),
                      "colour");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor,
                                       "c: rgb(1, 2)"),
                      "invalid");
}

/* ------------------------------------------------------------------ */
/* The value itself                                                   */
/* ------------------------------------------------------------------ */

static void TestCssComputedvalueLifecycle(void)
{
    CssComputedValue value = RoboCssCleared;
    CssComputedValue copy = RoboCssCleared;
    CssComputedValue moved = RoboCssCleared;
    CssComputedValue other = RoboCssCleared;

    CssFixture();
    CssComputedValueInit(&value);
    RoboCheckEqInt(CssComputedValueIsEmpty(&value), 1);
    RoboCheckEqInt(CssComputedValueIsEmpty(NULL), 1);
    RoboCheckStrEq(CssComputedValueKindName(value.kind), "none");

    RoboCheckEqInt(CssComputedValueMakeKeyword(
                          &value, CssSliceFromText("auto"), NULL),
                      RoboOk);
    RoboCheckStrEq(CssComputedValueKindName(value.kind), "keyword");
    RoboCheck(CssSliceEqualsAscii(CssComputedValueText(&value), "auto"));

    /* A copy is independent, and copying over a value disposes what was there. */
    RoboCheckEqInt(CssComputedValueCopy(&copy, &value, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&copy, &value), 1);
    RoboCheck(CssComputedValueText(&copy).bytes !=
               CssComputedValueText(&value).bytes);
    CssComputedValueDispose(&value);
    RoboCheckStrEq(CssComputedValueKindName(copy.kind), "keyword");
    RoboCheck(CssSliceEqualsAscii(CssComputedValueText(&copy), "auto"));

    /* A move leaves the source empty and the destination owning the bytes. */
    RoboCheckEqInt(CssComputedValueMove(&copy, &moved, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueIsEmpty(&copy), 1);
    RoboCheck(CssSliceEqualsAscii(CssComputedValueText(&moved), "auto"));
    RoboCheckEqInt(CssComputedValueMove(&moved, &moved, NULL),
                      RoboInvalidArgument);

    /* Two values of different kinds are never equal, and one of the same kind
     * with a different number is not either. */
    RoboCheckEqInt(CssComputedValueMakeNumber(&other, 1.0, 1, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&moved, &other), 0);
    /* A NULL value is only equal to another NULL: there is no value to compare
     * against, and answering 1 would make `equals` unusable as a guard. */
    RoboCheckEqInt(CssComputedValueEquals(&moved, NULL), 0);
    RoboCheckEqInt(CssComputedValueEquals(NULL, NULL), 1);

    CssComputedValueDispose(&other);
    CssComputedValueDispose(&moved);
    CssComputedValueDispose(&copy);
    CssComputedValueDispose(&value);
    CssComputedValueDispose(NULL);
}

static void TestCssComputedvalueLifetimeBalances(void)
{
    CssComputedValue value = RoboCssCleared;
    CssSource source = RoboCssCleared;
    int enabled;
    size_t baseline;
    size_t index;

    CssFixture();
    enabled = CssAllocatorEnsureEnabled();
    /* The live count is only a leak detector while the tracker is on, so the
     * fixture's job is to guarantee that rather than to report who turned it
     * on: this test reports `enabled` back to `restore` instead. A Release
     * build has no tracker to be on, so the guarantee is asserted where it can
     * hold and the rest of the test runs in both. */
#if RoboMemDebug
    RoboCheckEqInt(MemDebugIsEnabled(), 1);
#endif
    CssSourceInit(&source);

    for (index = 0u; index < 64u; index++) {
        CssComputedValue copy = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueMakeKeyword(
                              &value, CssSliceFromText("preserved"), NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedValueCopy(&copy, &value, NULL), RoboOk);
        CssComputedValueDispose(&copy);
        CssComputedValueDispose(&value);
    }
    /* Every iteration built and freed a value; the live count must be back where
     * it started, or something in the copy or the disposal leaked. */
    baseline = MemDebugLiveCount();
    for (index = 0u; index < 64u; index++) {
        RoboCheckEqInt(CssComputedValueMakeKeyword(
                              &value, CssSliceFromText("preserved"), NULL),
                          RoboOk);
        CssComputedValueDispose(&value);
    }
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);

    CssSourceDispose(&source);
    CssAllocatorRestore(enabled);
}

static void TestCssComputedvalueWrite(void)
{
    CssComputedValue value = RoboCssCleared;
    char text[RoboCssComputedValueTextMax];
    char small[4];
    size_t length = 0u;

    CssFixture();

    RoboCheckEqInt(CssComputedValueWrite(&value, text, sizeof(text), &length,
                                                    NULL),
                      RoboOk);
    RoboCheckStrEq(text, "");

    RoboCheckEqInt(CssComputedValueMakeKeyword(
                          &value, CssSliceFromText("auto"), NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueWrite(&value, text, sizeof(text), &length,
                                                    NULL),
                      RoboOk);
    RoboCheckStrEq(text, "auto");
    RoboCheckEqInt((int)length, 4);

    /* An integer number has no decimal point, and a fractional one has no
     * trailing zeroes: `10` and `1.5` rather than `10.000000` and `1.500000`. */
    RoboCheckEqInt(CssComputedValueMakeNumber(&value, 10.0, 1, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueWrite(&value, text, sizeof(text), &length,
                                                    NULL),
                      RoboOk);
    RoboCheckStrEq(text, "10");

    RoboCheckEqInt(CssComputedValueMakeNumber(&value, 1.5, 0, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueWrite(&value, text, sizeof(text), &length,
                                                    NULL),
                      RoboOk);
    RoboCheckStrEq(text, "1.5");

    RoboCheckEqInt(CssComputedValueMakePercentage(&value, 50.0, 1, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueWrite(&value, text, sizeof(text), &length,
                                                    NULL),
                      RoboOk);
    RoboCheckStrEq(text, "50%");

    RoboCheckEqInt(CssComputedValueMakeLength(&value, 10.0, RoboCssUnitPx, 1,
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueWrite(&value, text, sizeof(text), &length,
                                                    NULL),
                      RoboOk);
    RoboCheckStrEq(text, "10px");
    RoboCheckEqInt(CssComputedValueMakeLength(&value, 2.5, RoboCssUnitRem, 0,
                                                          NULL),
                      RoboOk);
    (void)CssComputedValueWrite(&value, text, sizeof(text), &length, NULL);
    RoboCheckStrEq(text, "2.5rem");
    /* A length with no unit is refused at construction: a number that claims to
     * be a length and has no unit is not a length. */
    RoboCheckEqInt(CssComputedValueMakeLength(&value, 1.0, RoboCssUnitNone, 1,
                                                          NULL),
                      RoboInvalidArgument);

    /* Alpha participates, and only when it is not opaque. */
    RoboCheckEqInt(CssComputedValueMakeColor(
                          &value, CssColorMake(0x33, 0x66, 0x99, 255), NULL),
                      RoboOk);
    (void)CssComputedValueWrite(&value, text, sizeof(text), &length, NULL);
    RoboCheckStrEq(text, "#336699");
    RoboCheckEqInt(CssComputedValueMakeColor(
                          &value, CssColorMake(0x33, 0x66, 0x99, 0x80), NULL),
                      RoboOk);
    (void)CssComputedValueWrite(&value, text, sizeof(text), &length, NULL);
    RoboCheckStrEq(text, "#33669980");

    RoboCheckEqInt(CssComputedValueMakeFamilyList(
                          &value, CssSliceFromText("Helvetica, Arial"), NULL),
                      RoboOk);
    (void)CssComputedValueWrite(&value, text, sizeof(text), &length, NULL);
    RoboCheckStrEq(text, "Helvetica, Arial");

    RoboCheckEqInt(CssComputedValueMakeInvalid(&value, "not a colour", NULL),
                      RoboOk);
    (void)CssComputedValueWrite(&value, text, sizeof(text), &length, NULL);
    RoboCheckStrEq(text, "not a colour");

    /* Too small is a refusal, not a truncation: a caller learns the length it
     * needed rather than reading half a value. */
    RoboCheckEqInt(CssComputedValueMakeKeyword(
                          &value, CssSliceFromText("auto"), NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueWrite(&value, small, sizeof(small), &length,
                                                    NULL),
                      RoboNotSupported);
    RoboCheckEqInt((int)length, 4);
    RoboCheckStrEq(small, "");

    RoboCheckEqInt(CssComputedValueWrite(NULL, text, sizeof(text), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComputedValueWrite(&value, NULL, sizeof(text), &length,
                                                    NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComputedValueWrite(&value, text, sizeof(text), NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComputedValueWrite(&value, text, 0u, &length, NULL),
                      RoboInvalidArgument);

    CssComputedValueDispose(&value);
}

/* ------------------------------------------------------------------ */
/* The normaliser                                                     */
/* ------------------------------------------------------------------ */

static void TestCssComputedvalueNormalizeKeyword(void)
{
    CssComputedValue value = RoboCssCleared;

    CssFixture();

    /* Canonicalised to lower case: `AUTO` and `auto` are one keyword, and a
     * computed value has to say so or `equals` is a comparison of typing. */
    CssComputeEquals(RoboCssPropertyComputationKeyword, "k: auto", "auto");
    CssComputeEquals(RoboCssPropertyComputationKeyword, "k: AUTO", "auto");
    CssComputeEquals(RoboCssPropertyComputationKeyword, "k: InLiNe", "inline");
    /* An escape changes which bytes spell a name, never which name it is. */
    CssComputeEquals(RoboCssPropertyComputationKeyword, "k: \\61 uto", "auto");

    /* `10` *is* one component — it is just not an identifier, and the message
     * says which of the two problems it is. */
    CssComputeEquals(RoboCssPropertyComputationKeyword, "k: 10", "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationKeyword, "k: auto auto",
                       "not a single identifier");
    CssComputeEquals(RoboCssPropertyComputationKeyword, "k: \"auto\"",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationKeyword, "k: rgb(1,2,3)",
                       "not a single identifier");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationKeyword, "k: auto"),
                      "keyword");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationKeyword, "k: 10"),
                      "invalid");

    /* A function that needs a later stage is preserved rather than rejected: the
     * author wrote something legal. */
    CssComputeEquals(RoboCssPropertyComputationKeyword, "k: var(--x)", "var(--x)");
    RoboCheckEqInt(CssComputeKind(RoboCssPropertyComputationKeyword,
                                       "k: var(--x)"),
                      "preserved");

    /* An unknown shape is refused rather than guessed at: computing a value
     * under a classification that does not exist has no right answer. */
    {
        CssValue specified = RoboCssCleared;
        CssSource source = RoboCssCleared;
        CssToken token = RoboCssCleared;

        CssSourceOfLiteral(&source, "auto");
        CssTokenOfText(&token, RoboCssTokenIdent, &source, "auto");
        RoboCheckEqInt(CssValueMakeToken(&specified, &token, NULL), RoboOk);
        RoboCheckEqInt(CssComputedValueCompute(
                              (CssPropertyComputation)99, &specified, &value,
                              NULL),
                          RoboInvalidArgument);
        CssValueDispose(&specified);
        CssSourceDispose(&source);
    }
    /* The classification helpers say the same thing. */
    RoboCheckEqInt(CssPropertyComputationIsKnown((
                          CssPropertyComputation)99),
                      0);
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationPreserve),
                      1);
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFontWeight),
                      "font-weight");
    RoboCheckStrEq(CssPropertyComputationName((
                          CssPropertyComputation)99),
                      "unknown");
    CssComputedValueDispose(&value);
}

static void TestCssComputedvalueNormalizeLength(void)
{
    CssFixture();

    /* px is the unit a computed length uses, so it survives unchanged. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 10px", "10px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 0px", "0px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: -3px", "-3px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 1.5px", "1.5px");

    /* Absolute units convert, exactly: `1in` and `96px` are the same length, so
     * two computed values that mean the same thing must be equal. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 1in", "96px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 1pc", "16px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 0pt", "0px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 72pt", "96px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 2.54cm", "96px");

    /* Relative units are *kept*. Converting `2em` here would be a number that
     * looks resolved and is wrong on every element with another font size. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 2em", "2em");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 1.5rem", "1.5rem");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 3ch", "3ch");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 50vh", "50vh");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 90vmin", "90vmin");

    /* A percentage stays a percentage: only layout knows what it is a
     * percentage *of*. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 50%", "50%");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLength, "l: 50%"),
                      "percentage");
    /* And 50% is not 50px, which is the distinction the kind exists to keep. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLength, "l: 50px"),
                      "length");

    /* CSS's one exception: a bare `0` is a length, because no unit changes what
     * zero means. Any other bare number is not. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 0", "0px");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 10",
                       "a bare number is not a length");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLength, "l: 10"),
                      "invalid");

    /* A length-family keyword is a keyword, because `auto` is the answer a width
     * can have and turning it into a number would be inventing one. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: auto", "auto");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: NORMAL", "normal");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLength, "l: auto"),
                      "keyword");

    /* An unknown unit is refused rather than ignored: `10furlongs` is not a
     * length, and reading it as `10` would be a wrong number. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 10furlongs",
                       "unknown length unit");
    /* `10pp` is a dimension whose unit nothing knows — a token the grammar
     * accepts and a length it does not. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 10pp",
                       "unknown length unit");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: 10px 20px",
                       "not a single length component");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: red",
                       "not a length keyword");
    CssComputeEquals(RoboCssPropertyComputationLength, "l: rgb(1,2,3)",
                       "not a single length component");

    /* `calc()` is a value only layout can reduce; it is carried, not rejected. */
    CssComputeEquals(RoboCssPropertyComputationLength, "l: calc(100% - 20px)",
                       "calc(100% - 20px)");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLength,
                                       "l: calc(100% - 20px)"),
                      "preserved");
}

static void TestCssComputedvalueNormalizeColor(void)
{
    CssComputedValue value = RoboCssCleared;
    uint32_t unresolved = 0u;

    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationColor, "c: red", "#ff0000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: RED", "#ff0000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: \\72 ed", "#ff0000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: transparent", "#00000000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: transparent",
                       "#00000000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: #f00", "#ff0000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: #F00", "#ff0000");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: #ff000080", "#ff000080");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: #336699", "#336699");

    /* Malformed input is an INVALID *answer*: the value was declared and was
     * wrong, and a caller can see that rather than finding the property absent. */
    /* Four digits is `#rgba`, which is a colour; two is nothing at all. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: #ff00", "#ffff0000");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor, "c: #ff"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor, "c: #ggg"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor, "c: #"), "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor, "c: nonsense"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor, "c: 10px"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor,
                                       "c: red blue"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor, "c: 42"),
                      "invalid");

    /* `currentColor` is a *typed* colour that is not resolved yet, and both
     * halves of that matter. Typed, so a consumer can see it is a colour rather
     * than three letters; unresolved, because the only context that can resolve
     * it is the element's own computed `color`, which the value layer has no way
     * to reach — RoboCSSComputed.c resolves it, against the parent's `color` when
     * the property *is* `color` and against the element's own otherwise. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: currentColor",
                       "currentColor");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor,
                                       "c: currentColor"),
                      "colour");
    CssComputeText(RoboCssPropertyComputationColor, "c: currentColor", NULL, 0u,
                     &unresolved);
    RoboCheckEqInt((int)unresolved, 1);
    /* One keyword, one computed value: how it was typed is not part of it. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: CURRENTCOLOR",
                       "currentColor");
    CssComputeEquals(RoboCssPropertyComputationColor, "c: currentcolor",
                       "currentColor");

    /* `hsl()` is a colour now, converted rather than carried: the modern
     * function used to be deferred and lands on the channels the hex spelling of
     * the same colour has. */
    CssComputeEquals(RoboCssPropertyComputationColor, "c: hsl(0, 100%, 50%)",
                       "#ff0000");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor,
                                       "c: hsl(0, 100%, 50%)"),
                      "colour");
    CssComputeText(RoboCssPropertyComputationColor, "c: hsl(0, 100%, 50%)", NULL, 0u,
                     &unresolved);
    RoboCheckEqInt((int)unresolved, 0);

    /* A colour space this build has no representation for is recognised and
     * carried, not guessed at: a wrong colour is worse than a deferred one, and
     * calling legal CSS invalid would be a lie about the author's stylesheet. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor,
                                       "c: oklch(0.5 0.1 200)"),
                      "preserved");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor,
                                       "c: color(display-p3 1 0 0)"),
                      "preserved");
    CssComputeText(RoboCssPropertyComputationColor, "c: oklch(0.5 0.1 200)", NULL, 0u,
                     &unresolved);
    RoboCheckEqInt((int)unresolved, 1);
    /* An *unknown* function is not a colour function, so it is invalid. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor,
                                       "c: sparkle(1)"),
                      "invalid");

    /* The colour the value carries is the colour that was written, read back
     * through the accessor rather than through the text. */
    {
        CssDeclarationList list = RoboCssCleared;
        CssError error = RoboCssCleared;
        const CssDeclaration *declaration;
        CssComputedValue computed = RoboCssCleared;
        CssColor color;

        RoboCheckEqInt(CssParseDeclarations("c: #33669980", &list, &error), RoboOk);
        declaration = CssDeclarationListAt(&list, 0u);
        RoboCheck(declaration != NULL);
        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationColor,
                              CssDeclarationValue(declaration), &computed, NULL),
                          RoboOk);
        color = CssComputedValueColor(&computed);
        RoboCheckEqInt(color.red, 0x33);
        RoboCheckEqInt(color.green, 0x66);
        RoboCheckEqInt(color.blue, 0x99);
        RoboCheckEqInt(color.alpha, 0x80);
        CssComputedValueDispose(&computed);
        CssDeclarationListDispose(&list);
    }
    /* Asking a non-colour for its colour answers transparent-black rather than
     * reading a union member that was never written. */
    RoboCheckEqInt(CssComputedValueMakeNumber(&value, 5.0, 1, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueColor(&value).alpha, 0);
    CssComputedValueDispose(&value);
}

static void TestCssComputedvalueNormalizeFonts(void)
{
    CssFixture();

    /* `font-family` keeps its order and is joined canonically, so the value is a
     * list rather than a coincidence of token order. */
    CssComputeEquals(RoboCssPropertyComputationFontFamily,
                       "f: Helvetica, Arial, sans-serif", "Helvetica, Arial, sans-serif");
    CssComputeEquals(RoboCssPropertyComputationFontFamily, "f: \"Times New Roman\"",
                       "Times New Roman");
    CssComputeEquals(RoboCssPropertyComputationFontFamily, "f: serif", "serif");
    CssComputeEquals(RoboCssPropertyComputationFontFamily, "f: A,b", "A, b");
    /* Malformed lists are invalid, including the two shapes that would otherwise
     * produce a subtly wrong list: a leading comma and two names with nothing
     * between them. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFontFamily,
                                       "f: ,serif"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFontFamily,
                                       "f: serif,"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFontFamily,
                                       "f: serif sans"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFontFamily,
                                       "f: \"\""),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFontFamily, "f: 10"),
                      "invalid");

    /* `font-weight` is a keyword or a number in range. */
    CssComputeEquals(RoboCssPropertyComputationFontWeight, "w: normal", "normal");
    CssComputeEquals(RoboCssPropertyComputationFontWeight, "w: BOLD", "bold");
    CssComputeEquals(RoboCssPropertyComputationFontWeight, "w: 700", "700");
    /* Out of range is invalid rather than clamped: 5000 is not a heavier 1000. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFontWeight,
                                       "w: 5000"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFontWeight, "w: 0"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFontWeight,
                                       "w: heavy"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFontWeight,
                                       "w: 3px"),
                      "invalid");

    /* `line-height` distinguishes a multiplier from a length, which is the whole
     * reason it has a shape of its own. */
    CssComputeEquals(RoboCssPropertyComputationLineHeight, "h: normal", "normal");
    CssComputeEquals(RoboCssPropertyComputationLineHeight, "h: 1.5", "1.5");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLineHeight, "h: 1.5"),
                      "number");
    CssComputeEquals(RoboCssPropertyComputationLineHeight, "h: 20px", "20px");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLineHeight, "h: 20px"),
                      "length");
    CssComputeEquals(RoboCssPropertyComputationLineHeight, "h: 150%", "150%");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLineHeight, "h: 150%"),
                      "percentage");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLineHeight,
                                       "h: auto"),
                      "keyword");
}

static void TestCssComputedvalueNormalizePreserve(void)
{
    CssComputedValue value = RoboCssCleared;
    uint32_t unresolved = 0u;

    CssFixture();

    /* A property whose shape nothing claims cannot have a value that is wrong
     * for it, so PRESERVE never answers invalid — it carries the tokens. */
    CssComputeEquals(RoboCssPropertyComputationPreserve, "p: 0 auto", "0 auto");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "p: medium none currentcolor",
                       "medium none currentcolor");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "p: url(a.png) no-repeat",
                       "url(a.png) no-repeat");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "p: totally-unknown",
                       "totally-unknown");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPreserve, "p: 0 auto"),
                      "preserved");
    /* A custom property is an opaque token sequence by definition. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPreserve,
                                       "p: 1px 2px 3px"),
                      "preserved");
    /* An empty value is still a declared value, and is preserved as one. */
    CssComputeEquals(RoboCssPropertyComputationPreserve, "p: \"\"", "\"\"");

    /* `var()` keeps its fallback tokens: the structure is the value here. */
    CssComputeEquals(RoboCssPropertyComputationPreserve,
                       "p: var(--x, blue)", "var(--x, blue)");
    CssComputeText(RoboCssPropertyComputationPreserve, "p: var(--x, blue)", NULL, 0u,
                     &unresolved);
    RoboCheckEqInt((int)unresolved, 1);

    /* A declaration the grammar could not make sense of never reaches this
     * function as a value, which is the guarantee that keeps a malformed
     * declaration from becoming a malformed computed value. */
    {
        CssDeclarationList list = RoboCssCleared;
        CssError error = RoboCssCleared;

        RoboCheckEqInt(CssParseDeclarations("; ; ;", &list, &error), RoboOk);
        RoboCheckEqInt((int)CssDeclarationListCount(&list), 0);
        CssDeclarationListDispose(&list);
    }
    /* NULL arguments and an unknown shape are all refused: the closed set is
     * the point of the parameter. */
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationPreserve, NULL, &value, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComputedValueCompute(
                          (CssPropertyComputation)RoboCssPropertyComputationCount,
                          NULL, &value, NULL),
                      RoboInvalidArgument);

    /* An empty (NONE) specified value is NONE, not invalid: "there was no
     * value" is a different fact from "this value means nothing". */
    {
        CssComputedValue none = RoboCssCleared;
        CssValue empty = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationPreserve, &empty, NULL, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationPreserve, &empty, &none, NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedValueIsEmpty(&none), 1);
        CssComputedValueDispose(&none);
    }
}

/**
 * Malformed input, at volume.
 *
 * Every string here is either a token sequence the grammar can produce or one it
 * refuses; none of them may crash, leak, or produce a kind outside the closed
 * set. The strings are generated from a table of hostile shapes rather than
 * typed out, so the coverage is broad without a long list.
 */
static void TestCssComputedvalueNormalizeRobust(void)
{
    static const char *const values[] = {
        "c: #", "c: #fffffffffffff", "c: ##", "c: -1px", "c: 1e999px", "c: 1e-999px",
        "c: rgb(", "c: rgb()", "c: rgb(,,,,)", "c: rgb(/ / /)", "c: rgb(1 2 3 4 5)",
        "c: rgba(1,2,3,4,5)", "c: currentcolor", "c: CURRENTCOLOR", "c: hsl(1)",
        "c: calc(", "c: calc())", "c: var(", "c: var(--", "c: var(--x", "c: var()",
        "c: \"unterminated", "c: url(", "c: \\", "c: 0.0.0", "c: 1 2 3 4 5 6 7 8 9",
        "c: inherit", "c: initial", "c: unset", "c: revert",
        "f: a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p", "f: \"", "f: ,,,",
        "l: 999999999999999999999999px", "l: -999999999999999999999999%",
        "l: 1e999", "l: 0em", "l: .5em", "l: +3PX",
        "w: 1e400", "h: 1e400", "k: 1e400", "p: { } [ ] ( )", "p: ;;;"
    };
    size_t index;

    CssFixture();
    for (index = 0u; index < sizeof(values) / sizeof(values[0]); index++) {
        static const CssPropertyComputation shapes[] = {
            RoboCssPropertyComputationColor,
            RoboCssPropertyComputationLength,
            RoboCssPropertyComputationKeyword,
            RoboCssPropertyComputationFontFamily,
            RoboCssPropertyComputationFontWeight,
            RoboCssPropertyComputationLineHeight,
            /* 9.12's shape is driven by the same hostile strings as the rest, so
             * a closed vocabulary is fuzzed for its *refusals*: what must not
             * happen is a value outside the four arriving as a DISPLAY value. */
            RoboCssPropertyComputationDisplay,
            /* 9.13 adds the second closed vocabulary and the first shape whose
             * value is a whole number, and both are driven by the same strings
             * for the same reason: `position: banana` must not arrive as a
             * POSITION value, and nothing may arrive as a NUMBER out of
             * `z-index` without `isInteger` set. */
            RoboCssPropertyComputationPosition,
            RoboCssPropertyComputationZIndex,
            RoboCssPropertyComputationPreserve
        };
        size_t shape;

        for (shape = 0u; shape < sizeof(shapes) / sizeof(shapes[0]); shape++) {
            CssDeclarationList list = RoboCssCleared;
            CssError error = RoboCssCleared;
            Result parsed;
            size_t declarationIndex;

            /* The grammar returns RoboOk whenever it produced a result,
             * recovering from what it could not use — so a hostile string
             * yields *something* to compute, and the assertions below are about
             * that something rather than about the parse. */
            parsed = CssParseDeclarations(values[index], &list, &error);
            RoboCheckEqInt(parsed, RoboOk);
            for (declarationIndex = 0u;
                 declarationIndex < CssDeclarationListCount(&list);
                 declarationIndex++) {
                const CssDeclaration *declaration =
                    CssDeclarationListAt(&list, declarationIndex);
                CssComputedValue computed = RoboCssCleared;
                char text[RoboCssComputedValueTextMax];
                size_t length = 0u;

                RoboCheckEqInt(CssComputedValueCompute(
                                      shapes[shape],
                                      CssDeclarationValue(declaration), &computed,
                                      NULL),
                                  RoboOk);
                /* Whatever it decided, it is a decision: a known kind, and a
                 * rendering that fits. */
                RoboCheckEqInt(CssComputedValueKindIsKnown(computed.kind), 1);
                RoboCheckEqInt(CssComputedValueWrite(&computed, text,
                                                               sizeof(text), &length, NULL),
                                  RoboOk);
                CssComputedValueDispose(&computed);
            }
            CssDeclarationListDispose(&list);
        }
    }
}

/* ------------------------------------------------------------------ */

void TestRegisterCssComputedValue(void);

void TestRegisterCssComputedValue(void)
{
    TestRegister("css.computedvalue.units", TestCssComputedvalueUnits);
    TestRegister("css.computedvalue.color_hex", TestCssComputedvalueColorHex);
    TestRegister("css.computedvalue.color_names", TestCssComputedvalueColorNames);
    TestRegister("css.computedvalue.color_named_table",
                       TestCssComputedvalueColorNamedTable);
    TestRegister("css.computedvalue.color_rgb_function",
                       TestCssComputedvalueColorRgbFunction);
    TestRegister("css.computedvalue.lifecycle", TestCssComputedvalueLifecycle);
    TestRegister("css.computedvalue.lifetime_balances",
                       TestCssComputedvalueLifetimeBalances);
    TestRegister("css.computedvalue.write", TestCssComputedvalueWrite);
    TestRegister("css.computedvalue.normalize_keyword",
                       TestCssComputedvalueNormalizeKeyword);
    TestRegister("css.computedvalue.normalize_length",
                       TestCssComputedvalueNormalizeLength);
    TestRegister("css.computedvalue.normalize_color",
                       TestCssComputedvalueNormalizeColor);
    TestRegister("css.computedvalue.normalize_fonts",
                       TestCssComputedvalueNormalizeFonts);
    TestRegister("css.computedvalue.normalize_preserve",
                       TestCssComputedvalueNormalizePreserve);
    TestRegister("css.computedvalue.normalize_robust",
                       TestCssComputedvalueNormalizeRobust);
}
