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

static void test_css_computedvalue_units(void)
{
    css_fixture();

    ROBO_CHECK_EQ_INT(robo_css_unit_is_known(ROBO_CSS_UNIT_PX), 1);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_known((robo_css_unit_t)ROBO_CSS_UNIT_COUNT), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_known(ROBO_CSS_UNIT_NONE), 1);

    ROBO_CHECK_STR_EQ(robo_css_unit_name(ROBO_CSS_UNIT_PX), "px");
    ROBO_CHECK_STR_EQ(robo_css_unit_name(ROBO_CSS_UNIT_REM), "rem");
    ROBO_CHECK_STR_EQ(robo_css_unit_name(ROBO_CSS_UNIT_VMIN), "vmin");
    /* A unit with no name is not a unit, and an empty name is the honest
     * rendering rather than a bogus one. */
    ROBO_CHECK_STR_EQ(robo_css_unit_name(ROBO_CSS_UNIT_NONE), "");
    ROBO_CHECK_STR_EQ(robo_css_unit_name((robo_css_unit_t)99), "");

    /* Case-insensitive, because CSS spells units that way. */
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text("PX"), ROBO_CSS_UNIT_PX);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text("Rem"), ROBO_CSS_UNIT_REM);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text("pxx"), ROBO_CSS_UNIT_NONE);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text(""), ROBO_CSS_UNIT_NONE);
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text(NULL), ROBO_CSS_UNIT_NONE);
    /* `%` is not a unit: it is its own kind, because 50% of a width and 50% of a
     * line height are different computations. */
    ROBO_CHECK_EQ_INT(robo_css_unit_from_text("%"), ROBO_CSS_UNIT_NONE);

    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_PX), 1);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_IN), 1);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_PT), 1);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_EM), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_VH), 0);
    ROBO_CHECK_EQ_INT(robo_css_unit_is_absolute(ROBO_CSS_UNIT_NONE), 0);

    /* CSS's own factors, and they are exact by definition: 1in is 96px, 1pt is
     * 4/3px, 1pc is 16px, 1cm is 96/2.54px. */
    ROBO_CHECK(robo_css_unit_to_px(1.0, ROBO_CSS_UNIT_PX) == 1.0);
    ROBO_CHECK(robo_css_unit_to_px(1.0, ROBO_CSS_UNIT_IN) == 96.0);
    ROBO_CHECK(robo_css_unit_to_px(1.0, ROBO_CSS_UNIT_PC) == 16.0);
    ROBO_CHECK(robo_css_unit_to_px(1.0, ROBO_CSS_UNIT_MM) > 3.77 &&
               robo_css_unit_to_px(1.0, ROBO_CSS_UNIT_MM) < 3.78);
    /* A relative unit has no factor, so the value passes through rather than
     * being multiplied by something invented. */
    ROBO_CHECK(robo_css_unit_to_px(2.0, ROBO_CSS_UNIT_EM) == 2.0);
    ROBO_CHECK(robo_css_unit_to_px(7.0, ROBO_CSS_UNIT_NONE) == 7.0);
}

/* ------------------------------------------------------------------ */
/* Colours                                                            */
/* ------------------------------------------------------------------ */

static void test_css_computedvalue_color_hex(void)
{
    robo_css_color_t color;

    css_fixture();

    /* Three digits: each nibble is *doubled*, which is the classic mistake —
     * `#f00` is (255, 0, 0) and not (15, 0, 0). */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("f00"), &color), 1);
    ROBO_CHECK_EQ_INT(color.red, 255);
    ROBO_CHECK_EQ_INT(color.green, 0);
    ROBO_CHECK_EQ_INT(color.blue, 0);
    ROBO_CHECK_EQ_INT(color.alpha, 255);

    /* Four: the fourth digit is alpha. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("0f08"), &color), 1);
    ROBO_CHECK_EQ_INT(color.red, 0);
    ROBO_CHECK_EQ_INT(color.green, 255);
    ROBO_CHECK_EQ_INT(color.blue, 0);
    ROBO_CHECK_EQ_INT(color.alpha, 136);

    /* Six and eight, mixed case. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("336699"), &color),
                      1);
    ROBO_CHECK_EQ_INT(color.red, 0x33);
    ROBO_CHECK_EQ_INT(color.green, 0x66);
    ROBO_CHECK_EQ_INT(color.blue, 0x99);
    ROBO_CHECK_EQ_INT(color.alpha, 255);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("AbCdEf80"), &color),
                      1);
    ROBO_CHECK_EQ_INT(color.red, 0xab);
    ROBO_CHECK_EQ_INT(color.green, 0xcd);
    ROBO_CHECK_EQ_INT(color.blue, 0xef);
    ROBO_CHECK_EQ_INT(color.alpha, 0x80);

    /* Every other length is refused rather than truncated: a three-digit colour
     * read as six would be a colour nobody wrote. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text(""), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("f"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("ff"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("fffff"), &color),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("fffffff"), &color),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("fffffffff"), &color),
                      0);
    /* And a non-hex digit is not a digit. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("ggg"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("12g456"), &color),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("ff 000"), &color),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_empty(), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("fff"), NULL), 0);
}

static void test_css_computedvalue_color_names(void)
{
    robo_css_color_t color;

    css_fixture();

    /* Representative names across the whole set, including the ones a partial
     * table would miss: a two-word name, an X11 oddity, and `rebeccapurple`. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("red"), &color),
                      1);
    ROBO_CHECK_EQ_INT(color.red, 255);
    ROBO_CHECK_EQ_INT(color.green, 0);
    ROBO_CHECK_EQ_INT(color.blue, 0);

    ROBO_CHECK_EQ_INT(
        robo_css_color_from_name(robo_css_slice_from_text("rebeccapurple"), &color), 1);
    ROBO_CHECK_EQ_INT(color.red, 0x66);
    ROBO_CHECK_EQ_INT(color.green, 0x33);
    ROBO_CHECK_EQ_INT(color.blue, 0x99);

    ROBO_CHECK_EQ_INT(
        robo_css_color_from_name(robo_css_slice_from_text("blanchedalmond"), &color), 1);
    ROBO_CHECK_EQ_INT(color.red, 0xff);
    ROBO_CHECK_EQ_INT(color.green, 0xeb);
    ROBO_CHECK_EQ_INT(color.blue, 0xcd);

    ROBO_CHECK_EQ_INT(
        robo_css_color_from_name(robo_css_slice_from_text("lightslategrey"), &color), 1);
    ROBO_CHECK_EQ_INT(color.red, 0x77);
    ROBO_CHECK_EQ_INT(color.blue, 0x99);

    /* The gray/grey pairs are the same colour under two spellings. */
    {
        robo_css_color_t g1;
        robo_css_color_t g2;

        ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("gray"), &g1),
                          1);
        ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("grey"), &g2),
                          1);
        ROBO_CHECK_EQ_INT(robo_css_color_equals(g1, g2), 1);
    }

    /* Case-insensitive. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("RED"), &color),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("ReBeCcApUrPlE"),
                                               &color),
                      1);

    /* `transparent` is a colour but not a named one: it is handled where the
     * keyword is, so a reader looking for it does not have to know it is spelled
     * as a table entry. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("transparent"),
                                               &color),
                      0);
    /* And an unknown word is not a colour rather than being silently black. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("reddish"), &color),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text(""), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_empty(), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("red"), NULL), 0);
}

/**
 * Every name in the standard set, through the public lookup.
 *
 * The point is coverage rather than the values: a table with a truncated or
 * misspelled entry would fail here, and a suite that checked only `red` would
 * not notice. The names are the same list CSS defines, spelled out because the
 * table itself is not readable from a test.
 */
static void test_css_computedvalue_color_named_table(void)
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
    robo_css_color_t color;
    size_t index;

    css_fixture();
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        ROBO_CHECK_EQ_INT(
            robo_css_color_from_name(robo_css_slice_from_text(names[index]), &color), 1);
        /* Every named colour is opaque; only `transparent` is not, and it is not
         * in this table. */
        ROBO_CHECK_EQ_INT(color.alpha, 255);
    }
    /* The count is the standard set's, spelled out: a silent deletion from the
     * table would otherwise be invisible. */
    ROBO_CHECK_EQ_INT((int)(sizeof(names) / sizeof(names[0])), 148);
}

static void test_css_computedvalue_color_rgb_function(void)
{
    /* The legacy comma syntax, with numbers. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(255, 0, 0)", "#ff0000");
    /* And with the alpha, numeric and as a percentage. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgba(255, 0, 0, 0.5)",
                       "#ff000080");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgba(0, 0, 255, 50%)",
                       "#0000ff80");
    /* Three arguments to `rgba()` is legal and means opaque. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgba(0, 128, 0)",
                       "#008000");
    /* The modern space syntax, with and without a slash. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(255 0 0)", "#ff0000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(255 0 0 / 0.25)",
                       "#ff000040");
    /* Percentages for channels. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(100%, 0%, 0%)",
                       "#ff0000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(50% 50% 50%)",
                       "#808080");
    /* Case-insensitive function name, and whitespace everywhere. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c:  RGB( 1 , 2 , 3 ) ",
                       "#010203");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: RGBA(1 2 3 / 1)",
                       "#010203");

    /* Clamped, not wrapped: `300` is 255 and `-10` is 0, which is what CSS says
     * and what keeps a plausible-but-wrong channel from appearing. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(300, -10, 0)",
                       "#ff0000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(0 0 0 / 5)", "#000000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(0 0 0 / -1)",
                       "#00000000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(200%, 0%, 0%)",
                       "#ff0000");

    /* Malformed calls are INVALID answers rather than crashes or colours. The
     * message is the function's own: a caller reporting `rgb()` as "not a
     * colour" tells the author nothing they can act on. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(255, 0)",
                       "rgb() arguments are out of range or malformed");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(1, 2, 3, 4, 5)",
                       "rgb() arguments are out of range or malformed");
    /* Mixing the two separator styles mid-call is malformed, not "close
     * enough": the comma list ends at the first space-separated pair. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(255, 0 0)",
                       "rgb() arguments are out of range or malformed");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(1 2, 3)",
                       "rgb() arguments are out of range or malformed");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(1, 2, 3 / 4)",
                       "rgb() arguments are out of range or malformed");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(, 2, 3)",
                       "rgb() arguments are out of range or malformed");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(1, 2,)",
                       "rgb() arguments are out of range or malformed");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(/ 1 2 3)",
                       "rgb() arguments are out of range or malformed");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(red, 0, 0)",
                       "rgb() arguments are out of range or malformed");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb(1 2 3 4)",
                       "rgb() arguments are out of range or malformed");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb()",
                       "rgb() arguments are out of range or malformed");
    /* A space around a comma is decoration, not a second separator. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: rgb( 1 , 2 , 3 )",
                       "#010203");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                       "c: rgb(1, 2, 3)"),
                      "colour");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                       "c: rgb(1, 2)"),
                      "invalid");
}

/* ------------------------------------------------------------------ */
/* The value itself                                                   */
/* ------------------------------------------------------------------ */

static void test_css_computedvalue_lifecycle(void)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_computed_value_t copy = ROBO_CSS_CLEARED;
    robo_css_computed_value_t moved = ROBO_CSS_CLEARED;
    robo_css_computed_value_t other = ROBO_CSS_CLEARED;

    css_fixture();
    robo_css_computed_value_init(&value);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_is_empty(&value), 1);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_is_empty(NULL), 1);
    ROBO_CHECK_STR_EQ(robo_css_computed_value_kind_name(value.kind), "none");

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                          &value, robo_css_slice_from_text("auto"), NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_css_computed_value_kind_name(value.kind), "keyword");
    ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_computed_value_text(&value), "auto"));

    /* A copy is independent, and copying over a value disposes what was there. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_copy(&copy, &value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_equals(&copy, &value), 1);
    ROBO_CHECK(robo_css_computed_value_text(&copy).bytes !=
               robo_css_computed_value_text(&value).bytes);
    robo_css_computed_value_dispose(&value);
    ROBO_CHECK_STR_EQ(robo_css_computed_value_kind_name(copy.kind), "keyword");
    ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_computed_value_text(&copy), "auto"));

    /* A move leaves the source empty and the destination owning the bytes. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_move(&copy, &moved, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_is_empty(&copy), 1);
    ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_computed_value_text(&moved), "auto"));
    ROBO_CHECK_EQ_INT(robo_css_computed_value_move(&moved, &moved, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Two values of different kinds are never equal, and one of the same kind
     * with a different number is not either. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_number(&other, 1.0, 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_equals(&moved, &other), 0);
    /* A NULL value is only equal to another NULL: there is no value to compare
     * against, and answering 1 would make `equals` unusable as a guard. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_equals(&moved, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_equals(NULL, NULL), 1);

    robo_css_computed_value_dispose(&other);
    robo_css_computed_value_dispose(&moved);
    robo_css_computed_value_dispose(&copy);
    robo_css_computed_value_dispose(&value);
    robo_css_computed_value_dispose(NULL);
}

static void test_css_computedvalue_lifetime_balances(void)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_source_t source = ROBO_CSS_CLEARED;
    int enabled;
    size_t baseline;
    size_t index;

    css_fixture();
    enabled = css_allocator_ensure_enabled();
    /* The live count is only a leak detector while the tracker is on, so the
     * fixture's job is to guarantee that rather than to report who turned it
     * on: this test reports `enabled` back to `restore` instead. A Release
     * build has no tracker to be on, so the guarantee is asserted where it can
     * hold and the rest of the test runs in both. */
#if ROBO_MEM_DEBUG
    ROBO_CHECK_EQ_INT(robo_mem_debug_is_enabled(), 1);
#endif
    robo_css_source_init(&source);

    for (index = 0u; index < 64u; index++) {
        robo_css_computed_value_t copy = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                              &value, robo_css_slice_from_text("preserved"), NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_copy(&copy, &value, NULL), ROBO_OK);
        robo_css_computed_value_dispose(&copy);
        robo_css_computed_value_dispose(&value);
    }
    /* Every iteration built and freed a value; the live count must be back where
     * it started, or something in the copy or the disposal leaked. */
    baseline = robo_mem_debug_live_count();
    for (index = 0u; index < 64u; index++) {
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                              &value, robo_css_slice_from_text("preserved"), NULL),
                          ROBO_OK);
        robo_css_computed_value_dispose(&value);
    }
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);

    robo_css_source_dispose(&source);
    css_allocator_restore(enabled);
}

static void test_css_computedvalue_write(void)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    char small[4];
    size_t length = 0u;

    css_fixture();

    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, sizeof(text), &length,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(text, "");

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                          &value, robo_css_slice_from_text("auto"), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, sizeof(text), &length,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(text, "auto");
    ROBO_CHECK_EQ_INT((int)length, 4);

    /* An integer number has no decimal point, and a fractional one has no
     * trailing zeroes: `10` and `1.5` rather than `10.000000` and `1.500000`. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_number(&value, 10.0, 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, sizeof(text), &length,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(text, "10");

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_number(&value, 1.5, 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, sizeof(text), &length,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(text, "1.5");

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_percentage(&value, 50.0, 1, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, sizeof(text), &length,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(text, "50%");

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, 10.0, ROBO_CSS_UNIT_PX, 1,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, sizeof(text), &length,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(text, "10px");
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, 2.5, ROBO_CSS_UNIT_REM, 0,
                                                          NULL),
                      ROBO_OK);
    (void)robo_css_computed_value_write(&value, text, sizeof(text), &length, NULL);
    ROBO_CHECK_STR_EQ(text, "2.5rem");
    /* A length with no unit is refused at construction: a number that claims to
     * be a length and has no unit is not a length. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, 1.0, ROBO_CSS_UNIT_NONE, 1,
                                                          NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Alpha participates, and only when it is not opaque. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_color(
                          &value, robo_css_color_make(0x33, 0x66, 0x99, 255), NULL),
                      ROBO_OK);
    (void)robo_css_computed_value_write(&value, text, sizeof(text), &length, NULL);
    ROBO_CHECK_STR_EQ(text, "#336699");
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_color(
                          &value, robo_css_color_make(0x33, 0x66, 0x99, 0x80), NULL),
                      ROBO_OK);
    (void)robo_css_computed_value_write(&value, text, sizeof(text), &length, NULL);
    ROBO_CHECK_STR_EQ(text, "#33669980");

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_family_list(
                          &value, robo_css_slice_from_text("Helvetica, Arial"), NULL),
                      ROBO_OK);
    (void)robo_css_computed_value_write(&value, text, sizeof(text), &length, NULL);
    ROBO_CHECK_STR_EQ(text, "Helvetica, Arial");

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_invalid(&value, "not a colour", NULL),
                      ROBO_OK);
    (void)robo_css_computed_value_write(&value, text, sizeof(text), &length, NULL);
    ROBO_CHECK_STR_EQ(text, "not a colour");

    /* Too small is a refusal, not a truncation: a caller learns the length it
     * needed rather than reading half a value. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                          &value, robo_css_slice_from_text("auto"), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, small, sizeof(small), &length,
                                                    NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)length, 4);
    ROBO_CHECK_STR_EQ(small, "");

    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(NULL, text, sizeof(text), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, NULL, sizeof(text), &length,
                                                    NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, sizeof(text), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text, 0u, &length, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_computed_value_dispose(&value);
}

/* ------------------------------------------------------------------ */
/* The normaliser                                                     */
/* ------------------------------------------------------------------ */

static void test_css_computedvalue_normalize_keyword(void)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;

    css_fixture();

    /* Canonicalised to lower case: `AUTO` and `auto` are one keyword, and a
     * computed value has to say so or `equals` is a comparison of typing. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: auto", "auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: AUTO", "auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: InLiNe", "inline");
    /* An escape changes which bytes spell a name, never which name it is. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: \\61 uto", "auto");

    /* `10` *is* one component — it is just not an identifier, and the message
     * says which of the two problems it is. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: 10", "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: auto auto",
                       "not a single identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: \"auto\"",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: rgb(1,2,3)",
                       "not a single identifier");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: auto"),
                      "keyword");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: 10"),
                      "invalid");

    /* A function that needs a later stage is preserved rather than rejected: the
     * author wrote something legal. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "k: var(--x)", "var(--x)");
    ROBO_CHECK_EQ_INT(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,
                                       "k: var(--x)"),
                      "preserved");

    /* An unknown shape is refused rather than guessed at: computing a value
     * under a classification that does not exist has no right answer. */
    {
        robo_css_value_t specified = ROBO_CSS_CLEARED;
        robo_css_source_t source = ROBO_CSS_CLEARED;
        robo_css_token_t token = ROBO_CSS_CLEARED;

        css_source_of_literal(&source, "auto");
        css_token_of_text(&token, ROBO_CSS_TOKEN_IDENT, &source, "auto");
        ROBO_CHECK_EQ_INT(robo_css_value_make_token(&specified, &token, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              (robo_css_property_computation_t)99, &specified, &value,
                              NULL),
                          ROBO_INVALID_ARGUMENT);
        robo_css_value_dispose(&specified);
        robo_css_source_dispose(&source);
    }
    /* The classification helpers say the same thing. */
    ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known((
                          robo_css_property_computation_t)99),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known(
                          ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE),
                      1);
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT),
                      "font-weight");
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name((
                          robo_css_property_computation_t)99),
                      "unknown");
    robo_css_computed_value_dispose(&value);
}

static void test_css_computedvalue_normalize_length(void)
{
    css_fixture();

    /* px is the unit a computed length uses, so it survives unchanged. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 10px", "10px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 0px", "0px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: -3px", "-3px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 1.5px", "1.5px");

    /* Absolute units convert, exactly: `1in` and `96px` are the same length, so
     * two computed values that mean the same thing must be equal. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 1in", "96px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 1pc", "16px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 0pt", "0px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 72pt", "96px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 2.54cm", "96px");

    /* Relative units are *kept*. Converting `2em` here would be a number that
     * looks resolved and is wrong on every element with another font size. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 2em", "2em");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 1.5rem", "1.5rem");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 3ch", "3ch");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 50vh", "50vh");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 90vmin", "90vmin");

    /* A percentage stays a percentage: only layout knows what it is a
     * percentage *of*. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 50%", "50%");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 50%"),
                      "percentage");
    /* And 50% is not 50px, which is the distinction the kind exists to keep. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 50px"),
                      "length");

    /* CSS's one exception: a bare `0` is a length, because no unit changes what
     * zero means. Any other bare number is not. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 0", "0px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 10",
                       "a bare number is not a length");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 10"),
                      "invalid");

    /* A length-family keyword is a keyword, because `auto` is the answer a width
     * can have and turning it into a number would be inventing one. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: auto", "auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: NORMAL", "normal");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: auto"),
                      "keyword");

    /* An unknown unit is refused rather than ignored: `10furlongs` is not a
     * length, and reading it as `10` would be a wrong number. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 10furlongs",
                       "unknown length unit");
    /* `10pp` is a dimension whose unit nothing knows — a token the grammar
     * accepts and a length it does not. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 10pp",
                       "unknown length unit");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: 10px 20px",
                       "not a single length component");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: red",
                       "not a length keyword");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: rgb(1,2,3)",
                       "not a single length component");

    /* `calc()` is a value only layout can reduce; it is carried, not rejected. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "l: calc(100% - 20px)",
                       "calc(100% - 20px)");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                                       "l: calc(100% - 20px)"),
                      "preserved");
}

static void test_css_computedvalue_normalize_color(void)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    uint32_t unresolved = 0u;

    css_fixture();

    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: red", "#ff0000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: RED", "#ff0000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: \\72 ed", "#ff0000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: transparent", "#00000000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: transparent",
                       "#00000000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: #f00", "#ff0000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: #F00", "#ff0000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: #ff000080", "#ff000080");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: #336699", "#336699");

    /* Malformed input is an INVALID *answer*: the value was declared and was
     * wrong, and a caller can see that rather than finding the property absent. */
    /* Four digits is `#rgba`, which is a colour; two is nothing at all. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: #ff00", "#ffff0000");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: #ff"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: #ggg"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: #"), "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: nonsense"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: 10px"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                       "c: red blue"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: 42"),
                      "invalid");

    /* `currentColor` is a *typed* colour that is not resolved yet, and both
     * halves of that matter. Typed, so a consumer can see it is a colour rather
     * than three letters; unresolved, because the only context that can resolve
     * it is the element's own computed `color`, which the value layer has no way
     * to reach — RoboCSSComputed.c resolves it, against the parent's `color` when
     * the property *is* `color` and against the element's own otherwise. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: currentColor",
                       "currentColor");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                       "c: currentColor"),
                      "colour");
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: currentColor", NULL, 0u,
                     &unresolved);
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
    /* One keyword, one computed value: how it was typed is not part of it. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: CURRENTCOLOR",
                       "currentColor");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: currentcolor",
                       "currentColor");

    /* `hsl()` is a colour now, converted rather than carried: the modern
     * function used to be deferred and lands on the channels the hex spelling of
     * the same colour has. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: hsl(0, 100%, 50%)",
                       "#ff0000");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                       "c: hsl(0, 100%, 50%)"),
                      "colour");
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: hsl(0, 100%, 50%)", NULL, 0u,
                     &unresolved);
    ROBO_CHECK_EQ_INT((int)unresolved, 0);

    /* A colour space this build has no representation for is recognised and
     * carried, not guessed at: a wrong colour is worse than a deferred one, and
     * calling legal CSS invalid would be a lie about the author's stylesheet. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                       "c: oklch(0.5 0.1 200)"),
                      "preserved");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                       "c: color(display-p3 1 0 0)"),
                      "preserved");
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "c: oklch(0.5 0.1 200)", NULL, 0u,
                     &unresolved);
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
    /* An *unknown* function is not a colour function, so it is invalid. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                       "c: sparkle(1)"),
                      "invalid");

    /* The colour the value carries is the colour that was written, read back
     * through the accessor rather than through the text. */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;
        const robo_css_declaration_t *declaration;
        robo_css_computed_value_t computed = ROBO_CSS_CLEARED;
        robo_css_color_t color;

        ROBO_CHECK_EQ_INT(css_parse_declarations("c: #33669980", &list, &error), ROBO_OK);
        declaration = robo_css_declaration_list_at(&list, 0u);
        ROBO_CHECK(declaration != NULL);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                              robo_css_declaration_value(declaration), &computed, NULL),
                          ROBO_OK);
        color = robo_css_computed_value_color(&computed);
        ROBO_CHECK_EQ_INT(color.red, 0x33);
        ROBO_CHECK_EQ_INT(color.green, 0x66);
        ROBO_CHECK_EQ_INT(color.blue, 0x99);
        ROBO_CHECK_EQ_INT(color.alpha, 0x80);
        robo_css_computed_value_dispose(&computed);
        robo_css_declaration_list_dispose(&list);
    }
    /* Asking a non-colour for its colour answers transparent-black rather than
     * reading a union member that was never written. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_number(&value, 5.0, 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_color(&value).alpha, 0);
    robo_css_computed_value_dispose(&value);
}

static void test_css_computedvalue_normalize_fonts(void)
{
    css_fixture();

    /* `font-family` keeps its order and is joined canonically, so the value is a
     * list rather than a coincidence of token order. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                       "f: Helvetica, Arial, sans-serif", "Helvetica, Arial, sans-serif");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY, "f: \"Times New Roman\"",
                       "Times New Roman");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY, "f: serif", "serif");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY, "f: A,b", "A, b");
    /* Malformed lists are invalid, including the two shapes that would otherwise
     * produce a subtly wrong list: a leading comma and two names with nothing
     * between them. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                       "f: ,serif"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                       "f: serif,"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                       "f: serif sans"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                       "f: \"\""),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY, "f: 10"),
                      "invalid");

    /* `font-weight` is a keyword or a number in range. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT, "w: normal", "normal");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT, "w: BOLD", "bold");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT, "w: 700", "700");
    /* Out of range is invalid rather than clamped: 5000 is not a heavier 1000. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,
                                       "w: 5000"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT, "w: 0"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,
                                       "w: heavy"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,
                                       "w: 3px"),
                      "invalid");

    /* `line-height` distinguishes a multiplier from a length, which is the whole
     * reason it has a shape of its own. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT, "h: normal", "normal");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT, "h: 1.5", "1.5");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT, "h: 1.5"),
                      "number");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT, "h: 20px", "20px");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT, "h: 20px"),
                      "length");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT, "h: 150%", "150%");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT, "h: 150%"),
                      "percentage");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT,
                                       "h: auto"),
                      "keyword");
}

static void test_css_computedvalue_normalize_preserve(void)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    uint32_t unresolved = 0u;

    css_fixture();

    /* A property whose shape nothing claims cannot have a value that is wrong
     * for it, so PRESERVE never answers invalid — it carries the tokens. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, "p: 0 auto", "0 auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, "p: medium none currentcolor",
                       "medium none currentcolor");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, "p: url(a.png) no-repeat",
                       "url(a.png) no-repeat");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, "p: totally-unknown",
                       "totally-unknown");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, "p: 0 auto"),
                      "preserved");
    /* A custom property is an opaque token sequence by definition. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,
                                       "p: 1px 2px 3px"),
                      "preserved");
    /* An empty value is still a declared value, and is preserved as one. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, "p: \"\"", "\"\"");

    /* `var()` keeps its fallback tokens: the structure is the value here. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE,
                       "p: var(--x, blue)", "var(--x, blue)");
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, "p: var(--x, blue)", NULL, 0u,
                     &unresolved);
    ROBO_CHECK_EQ_INT((int)unresolved, 1);

    /* A declaration the grammar could not make sense of never reaches this
     * function as a value, which is the guarantee that keeps a malformed
     * declaration from becoming a malformed computed value. */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(css_parse_declarations("; ; ;", &list, &error), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_css_declaration_list_count(&list), 0);
        robo_css_declaration_list_dispose(&list);
    }
    /* NULL arguments and an unknown shape are all refused: the closed set is
     * the point of the parameter. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                          ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, NULL, &value, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                          (robo_css_property_computation_t)ROBO_CSS_PROPERTY_COMPUTATION_COUNT,
                          NULL, &value, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* An empty (NONE) specified value is NONE, not invalid: "there was no
     * value" is a different fact from "this value means nothing". */
    {
        robo_css_computed_value_t none = ROBO_CSS_CLEARED;
        robo_css_value_t empty = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, &empty, NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, &empty, &none, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_is_empty(&none), 1);
        robo_css_computed_value_dispose(&none);
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
static void test_css_computedvalue_normalize_robust(void)
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

    css_fixture();
    for (index = 0u; index < sizeof(values) / sizeof(values[0]); index++) {
        static const robo_css_property_computation_t shapes[] = {
            ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
            ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
            ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,
            ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
            ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,
            ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT,
            /* 9.12's shape is driven by the same hostile strings as the rest, so
             * a closed vocabulary is fuzzed for its *refusals*: what must not
             * happen is a value outside the four arriving as a DISPLAY value. */
            ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY,
            /* 9.13 adds the second closed vocabulary and the first shape whose
             * value is a whole number, and both are driven by the same strings
             * for the same reason: `position: banana` must not arrive as a
             * POSITION value, and nothing may arrive as a NUMBER out of
             * `z-index` without `is_integer` set. */
            ROBO_CSS_PROPERTY_COMPUTATION_POSITION,
            ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX,
            ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE
        };
        size_t shape;

        for (shape = 0u; shape < sizeof(shapes) / sizeof(shapes[0]); shape++) {
            robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
            robo_css_error_t error = ROBO_CSS_CLEARED;
            robo_result_t parsed;
            size_t declaration_index;

            /* The grammar returns ROBO_OK whenever it produced a result,
             * recovering from what it could not use — so a hostile string
             * yields *something* to compute, and the assertions below are about
             * that something rather than about the parse. */
            parsed = css_parse_declarations(values[index], &list, &error);
            ROBO_CHECK_EQ_INT(parsed, ROBO_OK);
            for (declaration_index = 0u;
                 declaration_index < robo_css_declaration_list_count(&list);
                 declaration_index++) {
                const robo_css_declaration_t *declaration =
                    robo_css_declaration_list_at(&list, declaration_index);
                robo_css_computed_value_t computed = ROBO_CSS_CLEARED;
                char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
                size_t length = 0u;

                ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                                      shapes[shape],
                                      robo_css_declaration_value(declaration), &computed,
                                      NULL),
                                  ROBO_OK);
                /* Whatever it decided, it is a decision: a known kind, and a
                 * rendering that fits. */
                ROBO_CHECK_EQ_INT(robo_css_computed_value_kind_is_known(computed.kind), 1);
                ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&computed, text,
                                                               sizeof(text), &length, NULL),
                                  ROBO_OK);
                robo_css_computed_value_dispose(&computed);
            }
            robo_css_declaration_list_dispose(&list);
        }
    }
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_computed_value(void);

void robo_test_register_css_computed_value(void)
{
    robo_test_register("css.computedvalue.units", test_css_computedvalue_units);
    robo_test_register("css.computedvalue.color_hex", test_css_computedvalue_color_hex);
    robo_test_register("css.computedvalue.color_names", test_css_computedvalue_color_names);
    robo_test_register("css.computedvalue.color_named_table",
                       test_css_computedvalue_color_named_table);
    robo_test_register("css.computedvalue.color_rgb_function",
                       test_css_computedvalue_color_rgb_function);
    robo_test_register("css.computedvalue.lifecycle", test_css_computedvalue_lifecycle);
    robo_test_register("css.computedvalue.lifetime_balances",
                       test_css_computedvalue_lifetime_balances);
    robo_test_register("css.computedvalue.write", test_css_computedvalue_write);
    robo_test_register("css.computedvalue.normalize_keyword",
                       test_css_computedvalue_normalize_keyword);
    robo_test_register("css.computedvalue.normalize_length",
                       test_css_computedvalue_normalize_length);
    robo_test_register("css.computedvalue.normalize_color",
                       test_css_computedvalue_normalize_color);
    robo_test_register("css.computedvalue.normalize_fonts",
                       test_css_computedvalue_normalize_fonts);
    robo_test_register("css.computedvalue.normalize_preserve",
                       test_css_computedvalue_normalize_preserve);
    robo_test_register("css.computedvalue.normalize_robust",
                       test_css_computedvalue_normalize_robust);
}
