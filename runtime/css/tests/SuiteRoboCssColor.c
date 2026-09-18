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
static const robo_css_value_t *css_color_value_of(const char *text,
                                                 robo_css_declaration_list_t *list)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    css_fixture();
    ROBO_CHECK_EQ_INT(css_parse_declarations(text, list, &error), ROBO_OK);
    /* One declaration out, which is the guard that matters: a fixture that
     * produced none would make every expectation about a NULL value. The error
     * *record* is deliberately not asserted on, because a colour value that is
     * malformed is usually a grammar-level error too (`rgb(` is an unclosed
     * function) and the subject of these tests is the colour parser's verdict on
     * a value the grammar did produce — not the grammar's. */
    ROBO_CHECK_EQ_INT((int)robo_css_declaration_list_count(list), 1);
    return robo_css_declaration_value(robo_css_declaration_list_at(list, 0u));
}

/**
 * Parse the colour in @p declaration text, returning what came out.
 *
 * The colour record holds only integers, so it is safe to read after the list is
 * disposed — which is worth doing rather than leaking the list into every
 * assertion's scope.
 */
static robo_css_color_parse_t css_color_parse(const char *declaration,
                                             robo_css_color_t *out)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value = css_color_value_of(declaration, &list);
    robo_css_color_parse_t outcome;

    ROBO_CHECK(value != NULL);
    outcome = robo_css_color_from_value(value, out);
    robo_css_declaration_list_dispose(&list);
    return outcome;
}

/** Assert @p color writes as @p expected, and that the length agrees. */
static void css_color_written_is(robo_css_color_t color, const char *expected)
{
    char text[ROBO_CSS_COLOR_TEXT_MAX];
    size_t length = 0u;

    ROBO_CHECK_EQ_INT(robo_css_color_write(color, text, sizeof(text), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(text, expected);
    ROBO_CHECK_EQ_INT(length, strlen(expected));
}

/**
 * Assert @p declaration is a colour and writes back as @p expected.
 *
 * The written form is the assertion and not the channels, deliberately: two
 * spellings of one colour compare equal *because* they serialize to the same
 * text, so a test that compared channels would pass while the two forms could
 * still disagree about alpha or about how they print.
 */
static void css_color_is(const char *declaration, const char *expected)
{
    robo_css_color_t color = robo_css_color_none();

    ROBO_CHECK_EQ_INT((int)css_color_parse(declaration, &color), ROBO_CSS_COLOR_PARSE_RESOLVED);
    css_color_written_is(color, expected);
}

/** Assert @p declaration's colour parses to @p outcome. */
static void css_color_outcome_is(const char *declaration, robo_css_color_parse_t outcome)
{
    robo_css_color_t color = robo_css_color_none();

    ROBO_CHECK_EQ_INT((int)css_color_parse(declaration, &color), (int)outcome);
}

/** Assert @p declaration is a colour whose channels are exactly as given. */
static void css_color_channels_are(const char *declaration, unsigned red, unsigned green,
                                   unsigned blue, unsigned alpha)
{
    robo_css_color_t color = robo_css_color_none();

    ROBO_CHECK_EQ_INT((int)css_color_parse(declaration, &color), ROBO_CSS_COLOR_PARSE_RESOLVED);
    ROBO_CHECK_EQ_INT(color.kind, ROBO_CSS_COLOR_RGBA);
    ROBO_CHECK_EQ_INT(color.red, red);
    ROBO_CHECK_EQ_INT(color.green, green);
    ROBO_CHECK_EQ_INT(color.blue, blue);
    ROBO_CHECK_EQ_INT(color.alpha, alpha);
}

/** Assert @p actual is exactly @p expected, naming @p label when they differ. */
static void css_color_equals_message(const robo_css_color_t actual,
                                     const robo_css_color_t expected, const char *label)
{
    if (!robo_css_color_equals(actual, expected)) {
        robo_test_fail(__FILE__, __LINE__, "%s: colours differ", label);
    }
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void test_color_records(void)
{
    robo_css_color_t none;
    robo_css_color_t invalid;
    robo_css_color_t current;
    robo_css_color_t red;
    robo_css_color_kind_t kind;

    css_fixture();
    none = robo_css_color_none();
    invalid = robo_css_color_invalid();
    current = robo_css_color_current();
    red = robo_css_color_make(255u, 0u, 0u, 255u);

    /* Every kind is known and named, and an unknown one says so rather than
     * borrowing a real kind's name. */
    for (kind = ROBO_CSS_COLOR_NONE; kind < (robo_css_color_kind_t)ROBO_CSS_COLOR_KIND_COUNT;
         kind = (robo_css_color_kind_t)(kind + 1)) {
        const char *name = robo_css_color_kind_name(kind);

        ROBO_CHECK(robo_css_color_kind_is_known(kind));
        ROBO_CHECK(name != NULL);
        ROBO_CHECK(strcmp(name, "unknown") != 0);
    }
    ROBO_CHECK_EQ_INT(robo_css_color_kind_is_known((robo_css_color_kind_t)99), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_kind_is_known((robo_css_color_kind_t)-1), 0);
    ROBO_CHECK_STR_EQ(robo_css_color_kind_name((robo_css_color_kind_t)99), "unknown");
    ROBO_CHECK_STR_EQ(robo_css_color_kind_name(ROBO_CSS_COLOR_RGBA), "rgba");
    ROBO_CHECK_STR_EQ(robo_css_color_kind_name(ROBO_CSS_COLOR_CURRENT), "current");
    ROBO_CHECK_STR_EQ(robo_css_color_kind_name(ROBO_CSS_COLOR_INVALID), "invalid");
    ROBO_CHECK_STR_EQ(robo_css_color_kind_name(ROBO_CSS_COLOR_NONE), "none");

    /* A record declared with the cleared initializer *is* NONE, which is what
     * makes `robo_css_color_t x = ROBO_CSS_CLEARED;` a usable starting point
     * rather than an uninitialized read. */
    {
        robo_css_color_t cleared = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(cleared.kind, ROBO_CSS_COLOR_NONE);
        ROBO_CHECK(robo_css_color_equals(cleared, none));
    }

    ROBO_CHECK(robo_css_color_is_resolved(red));
    ROBO_CHECK(!robo_css_color_is_current(red));
    ROBO_CHECK(robo_css_color_is_valid(red));
    ROBO_CHECK(robo_css_color_is_current(current));
    ROBO_CHECK(robo_css_color_is_valid(current));
    ROBO_CHECK(!robo_css_color_is_resolved(current));
    ROBO_CHECK(!robo_css_color_is_current(none));
    ROBO_CHECK(!robo_css_color_is_valid(none));
    ROBO_CHECK(!robo_css_color_is_valid(invalid));
    ROBO_CHECK(!robo_css_color_is_resolved(invalid));

    /* Equality is kind-aware, and that is the property that keeps a contextual
     * keyword from being mistaken for a channel set. */
    ROBO_CHECK(robo_css_color_equals(red, robo_css_color_make(255u, 0u, 0u, 255u)));
    ROBO_CHECK(!robo_css_color_equals(red, robo_css_color_make(255u, 0u, 0u, 254u)));
    ROBO_CHECK(!robo_css_color_equals(red, robo_css_color_make(254u, 0u, 0u, 255u)));
    ROBO_CHECK(!robo_css_color_equals(red, robo_css_color_make(255u, 1u, 0u, 255u)));
    ROBO_CHECK(!robo_css_color_equals(red, robo_css_color_make(255u, 0u, 1u, 255u)));
    ROBO_CHECK(!robo_css_color_equals(current, red));
    ROBO_CHECK(!robo_css_color_equals(current, robo_css_color_make(0u, 0u, 0u, 0u)));
    ROBO_CHECK(robo_css_color_equals(current, robo_css_color_current()));
    ROBO_CHECK(robo_css_color_equals(none, robo_css_color_none()));
    ROBO_CHECK(robo_css_color_equals(invalid, robo_css_color_invalid()));
    ROBO_CHECK(!robo_css_color_equals(none, invalid));
    ROBO_CHECK(!robo_css_color_equals(none, current));
}

static void test_color_parse_outcomes(void)
{
    robo_css_color_parse_t outcome;

    css_fixture();
    for (outcome = ROBO_CSS_COLOR_PARSE_INVALID;
         outcome < (robo_css_color_parse_t)ROBO_CSS_COLOR_PARSE_COUNT;
         outcome = (robo_css_color_parse_t)(outcome + 1)) {
        const char *name = robo_css_color_parse_name(outcome);

        ROBO_CHECK(robo_css_color_parse_is_known(outcome));
        ROBO_CHECK(name != NULL);
        ROBO_CHECK(strcmp(name, "unknown") != 0);
    }
    ROBO_CHECK_EQ_INT(robo_css_color_parse_is_known((robo_css_color_parse_t)99), 0);
    ROBO_CHECK_STR_EQ(robo_css_color_parse_name((robo_css_color_parse_t)99), "unknown");
    ROBO_CHECK_STR_EQ(robo_css_color_parse_name(ROBO_CSS_COLOR_PARSE_RESOLVED), "resolved");
    ROBO_CHECK_STR_EQ(robo_css_color_parse_name(ROBO_CSS_COLOR_PARSE_CURRENT), "current");
    ROBO_CHECK_STR_EQ(robo_css_color_parse_name(ROBO_CSS_COLOR_PARSE_DEFERRED), "deferred");
    ROBO_CHECK_STR_EQ(robo_css_color_parse_name(ROBO_CSS_COLOR_PARSE_MALFORMED), "malformed");
    ROBO_CHECK_STR_EQ(robo_css_color_parse_name(ROBO_CSS_COLOR_PARSE_UNKNOWN_NAME),
                      "unknown-name");
}

/* ------------------------------------------------------------------ */
/* The standard named set                                              */
/* ------------------------------------------------------------------ */

static void test_color_named_representative(void)
{
    robo_css_color_t color = robo_css_color_none();

    css_fixture();

    /* The CSS2.1 keywords, the SVG names, and the one CSS added afterwards. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("red"), &color), 1);
    css_color_written_is(color, "#ff0000");
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("rebeccapurple"),
                                               &color),
                      1);
    css_color_written_is(color, "#663399");
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("blanchedalmond"),
                                               &color),
                      1);
    css_color_written_is(color, "#ffebcd");
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("lightslategrey"),
                                               &color),
                      1);
    css_color_written_is(color, "#778899");

    /* Names are ASCII case-insensitive, because CSS keywords are. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("RED"), &color), 1);
    css_color_written_is(color, "#ff0000");
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("ReBeCcApUrPlE"),
                                               &color),
                      1);
    css_color_written_is(color, "#663399");

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
            robo_css_color_t first = robo_css_color_none();
            robo_css_color_t second = robo_css_color_none();

            ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text(pairs[index][0]),
                                                       &first),
                              1);
            ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text(pairs[index][1]),
                                                       &second),
                              1);
            ROBO_CHECK(robo_css_color_equals(first, second));
        }
    }

    /* An unknown word is not a colour — and not black either, which is the
     * silent-default failure this return value exists to prevent. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("reddish"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text(""), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_empty(), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("red"), NULL), 0);
    /* `transparent` is a colour but not a *named* one: it is answered where the
     * keyword is, so that a reader looking for it does not have to know it is
     * spelled as a table entry. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("transparent"),
                                               &color),
                      0);
    css_color_is("c: transparent", "#00000000");
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
static void test_color_named_table(void)
{
    size_t count;
    size_t index;

    css_fixture();
    count = robo_css_color_named_count();
    ROBO_CHECK_EQ_INT(count, 148u);
    ROBO_CHECK_EQ_INT(robo_css_color_named_at(count, NULL, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_named_at((size_t)-1, NULL, NULL), 0);

    for (index = 0u; index < count; index++) {
        robo_css_slice_t name = robo_css_slice_empty();
        robo_css_color_t by_index = robo_css_color_none();
        robo_css_color_t by_name = robo_css_color_none();
        size_t byte;
        size_t other;

        ROBO_CHECK_EQ_INT(robo_css_color_named_at(index, &name, &by_index), 1);
        ROBO_CHECK(name.length > 0u);
        ROBO_CHECK(robo_css_color_is_resolved(by_index));
        ROBO_CHECK_EQ_INT(by_index.alpha, 255u);

        /* Present *and* reachable: the table hands it out and the lookup finds
         * the same colour, which is what makes the two doors one table. */
        ROBO_CHECK_EQ_INT(robo_css_color_from_name(name, &by_name), 1);
        ROBO_CHECK(robo_css_color_equals(by_index, by_name));

        for (byte = 0u; byte < name.length; byte++) {
            uint8_t value = name.bytes[byte];

            ROBO_CHECK(value >= (uint8_t)'a' && value <= (uint8_t)'z');
        }
        for (other = 0u; other < index; other++) {
            robo_css_slice_t earlier = robo_css_slice_empty();

            ROBO_CHECK_EQ_INT(robo_css_color_named_at(other, &earlier, NULL), 1);
            if (robo_css_slice_equals(earlier, name)) {
                robo_test_fail(__FILE__, __LINE__, "duplicate named colour: %.*s",
                               (int)name.length, (const char *)name.bytes);
            }
        }
    }
    /* Both outputs are optional, and a caller that only wants the name gets it. */
    {
        robo_css_slice_t name = robo_css_slice_empty();

        ROBO_CHECK_EQ_INT(robo_css_color_named_at(0u, &name, NULL), 1);
        ROBO_CHECK(name.length > 0u);
    }
}

/* ------------------------------------------------------------------ */
/* Hex                                                                 */
/* ------------------------------------------------------------------ */

static void test_color_hex(void)
{
    robo_css_color_t color = robo_css_color_none();

    css_fixture();

    /* `#rgb` doubles each nibble rather than zero-padding it: `#f00` is red, and
     * reading it as (0x0f, 0x00, 0x00) is the classic three-digit mistake. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("f00"), &color), 1);
    css_color_written_is(color, "#ff0000");
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("0f0"), &color), 1);
    css_color_written_is(color, "#00ff00");
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("abc"), &color), 1);
    css_color_written_is(color, "#aabbcc");

    /* `#rgba` carries alpha in the fourth nibble. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("0f08"), &color), 1);
    css_color_written_is(color, "#00ff0088");
    /* ...and an alpha nibble that is opaque normalises away: `#f00f` is `#ff0000`
     * exactly, so writing it back with a redundant `ff` would be the one place
     * this representation disagreed with the `#rrggbb` spelling of itself. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("f00f"), &color), 1);
    css_color_written_is(color, "#ff0000");
    ROBO_CHECK_EQ_INT(color.alpha, 255u);

    /* `#rrggbb` is opaque, `#rrggbbaa` is not. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("336699"), &color), 1);
    css_color_written_is(color, "#336699");
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("33669980"), &color), 1);
    css_color_written_is(color, "#33669980");

    /* Hexadecimal digits are case-insensitive, and the *stored* colour is the
     * same one either way round. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("AbCdEf80"), &color), 1);
    css_color_written_is(color, "#abcdef80");
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("abcdef80"), &color), 1);
    css_color_written_is(color, "#abcdef80");

    /* The four lengths and nothing else: short, long and almost-right are all
     * refused rather than padded or truncated. */
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text(""), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("f"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("ff"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("fffff"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("fffffff"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("fffffffff"), &color),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("ggg"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("12g456"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("ff 000"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("0x1234"), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_empty(), &color), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_from_hex(robo_css_slice_from_text("fff"), NULL), 0);

    /* Through the whole pipeline, a hash token is the same colour: the tokenizer
     * strips the `#` and the value accessors are what the parser reads. */
    css_color_is("c: #f00", "#ff0000");
    css_color_is("c: #F00", "#ff0000");
    css_color_is("c: #336699", "#336699");
    css_color_is("c: #33669980", "#33669980");
    css_color_is("c: #0f08", "#00ff0088");

    /* A hash spelling that is not one of the four lengths is not a colour. */
    css_color_outcome_is("c: #ff", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: #ggg", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: #fffffff", ROBO_CSS_COLOR_PARSE_MALFORMED);
}

/* ------------------------------------------------------------------ */
/* rgb() / rgba()                                                      */
/* ------------------------------------------------------------------ */

static void test_color_rgb(void)
{
    css_fixture();

    /* Numeric channels, both spellings of the function. */
    css_color_is("c: rgb(255, 0, 0)", "#ff0000");
    css_color_is("c: rgba(255, 0, 0)", "#ff0000");
    css_color_is("c: rgb(0, 128, 0)", "#008000");
    css_color_is("c: rgb(51, 102, 153)", "#336699");

    /* Percentage channels, and the fact that 100% is 255 rather than 100. */
    css_color_is("c: rgb(100%, 0%, 0%)", "#ff0000");
    css_color_is("c: rgb(20%, 40%, 60%)", "#336699");
    css_color_is("c: rgb(50%, 50%, 50%)", "#808080");
    /* 51% of 255 is 130.05, which lands on 130: the channel is rounded once, at
     * the end, rather than at each step of the conversion. */
    css_color_is("c: rgb(51%, 40%, 60%)", "#826699");
    /* Mixed number and percentage channels are CSS, and are not mixed up. */
    css_color_is("c: rgb(255, 0%, 0)", "#ff0000");

    /* Alpha: a number, a percentage, after the legacy comma and after the
     * modern slash. */
    css_color_is("c: rgba(255, 0, 0, 0.5)", "#ff000080");
    css_color_is("c: rgba(255, 0, 0, 0)", "#ff000000");
    css_color_is("c: rgba(255, 0, 0, 1)", "#ff0000");
    css_color_is("c: rgba(255, 0, 0, 50%)", "#ff000080");
    css_color_is("c: rgb(255 0 0 / 50%)", "#ff000080");
    css_color_is("c: rgb(255 0 0 / 0.25)", "#ff000040");

    /* The modern space syntax, and whitespace everywhere it is legal. */
    css_color_is("c: rgb(255 0 0)", "#ff0000");
    css_color_is("c: rgb( 255   0   0 )", "#ff0000");
    css_color_is("c: rgb( 255 , 0 , 0 )", "#ff0000");
    /* Percentage channels in the space syntax too, and 50% is 128 rather than
     * 127: the channel is rounded to nearest, not truncated. */
    css_color_is("c: rgb(0% 50% 100%)", "#0080ff");

    /* Channels clamp rather than wrap: 300 is red, not a colour with a wrapped
     * green channel, and a negative channel is zero. */
    css_color_channels_are("c: rgb(300, 0, 0)", 255u, 0u, 0u, 255u);
    css_color_channels_are("c: rgb(-5, 0, 0)", 0u, 0u, 0u, 255u);
    css_color_channels_are("c: rgb(200%, 0%, 0%)", 255u, 0u, 0u, 255u);
    css_color_channels_are("c: rgba(255, 0, 0, 2)", 255u, 0u, 0u, 255u);
    css_color_channels_are("c: rgba(255, 0, 0, -1)", 255u, 0u, 0u, 0u);

    /* Fractional channels round to the nearest 8-bit step, once, at the end. */
    css_color_channels_are("c: rgb(0.5, 1.5, 2.5)", 1u, 2u, 3u, 255u);
    css_color_channels_are("c: rgba(0, 0, 0, 0.5)", 0u, 0u, 0u, 128u);

    /* Both spellings of the function accept both shapes, which is CSS Color 4's
     * rule and the reason `rgb` and `rgba` are not distinguished here. */
    css_color_is("c: rgb(255, 0, 0, 0.5)", "#ff000080");
    css_color_is("c: rgba(255 0 0)", "#ff0000");

    /* Not a colour: nothing, too few, too many, the wrong separator, a
     * dimension where a channel belongs, or a nested function. */
    css_color_outcome_is("c: rgb()", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(1)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(1, 2)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(1, 2, 3, 4, 5)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(1 2 3 4)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(255, 0 0)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(1, 2, 3 / 4)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(10px, 0, 0)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(255, 0)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(red, 0, 0)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(1e999, 0, 0)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: rgb(currentColor, 0, 0)", ROBO_CSS_COLOR_PARSE_MALFORMED);

    /* A hand-built call — no source, no children — is refused rather than read
     * past its end. */
    {
        robo_css_value_t function = ROBO_CSS_CLEARED;
        robo_css_color_t parsed = robo_css_color_none();

        ROBO_CHECK_EQ_INT(robo_css_value_make_function(&function,
                                                       robo_css_slice_from_text("rgb"),
                                                       robo_css_range_at(0u), NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_color_from_rgb_function(&function, &parsed), 0);
        ROBO_CHECK_EQ_INT(robo_css_color_from_function(&function, &parsed),
                          ROBO_CSS_COLOR_PARSE_MALFORMED);
        robo_css_value_dispose(&function);
        ROBO_CHECK_EQ_INT(robo_css_color_from_rgb_function(NULL, &parsed), 0);
        ROBO_CHECK_EQ_INT((int)robo_css_color_from_function(NULL, &parsed),
                          (int)ROBO_CSS_COLOR_PARSE_INVALID);
    }
}

/* ------------------------------------------------------------------ */
/* hsl() / hsla()                                                      */
/* ------------------------------------------------------------------ */

/** Assert two declarations produce the *same* colour, whatever it is. */
static void css_color_same(const char *left, const char *right)
{
    robo_css_color_t first = robo_css_color_none();
    robo_css_color_t second = robo_css_color_none();

    ROBO_CHECK_EQ_INT((int)css_color_parse(left, &first), (int)ROBO_CSS_COLOR_PARSE_RESOLVED);
    ROBO_CHECK_EQ_INT((int)css_color_parse(right, &second), (int)ROBO_CSS_COLOR_PARSE_RESOLVED);
    if (!robo_css_color_equals(first, second)) {
        char first_text[ROBO_CSS_COLOR_TEXT_MAX];
        char second_text[ROBO_CSS_COLOR_TEXT_MAX];
        size_t length = 0u;

        /* Both writes are checked, because a failed write leaves the buffer
         * untouched and the message would then name stack noise as the two
         * colours — which is worse than no message. */
        first_text[0] = '\0';
        second_text[0] = '\0';
        if (robo_css_color_write(first, first_text, sizeof(first_text), &length, NULL) !=
            ROBO_OK) {
            first_text[0] = '\0';
        }
        if (robo_css_color_write(second, second_text, sizeof(second_text), &length,
                                 NULL) != ROBO_OK) {
            second_text[0] = '\0';
        }
        robo_test_fail(__FILE__, __LINE__, "%s and %s differ: %s vs %s", left, right,
                       first_text, second_text);
    }
}

static void test_color_hsl(void)
{
    robo_css_color_t color = robo_css_color_none();

    css_fixture();

    /* The canonical cases, and they are canonical because they are the primary
     * colours: a conversion that is wrong by a sector lands somewhere else. */
    css_color_is("c: hsl(0, 100%, 50%)", "#ff0000");
    css_color_is("c: hsl(120, 100%, 25%)", "#008000");
    css_color_is("c: hsl(240, 100%, 50%)", "#0000ff");
    css_color_is("c: hsl(0, 0%, 0%)", "#000000");
    css_color_is("c: hsl(0, 0%, 100%)", "#ffffff");
    css_color_is("c: hsl(180, 100%, 50%)", "#00ffff");

    /* The conversion agrees with the named set and with the hex spelling of the
     * same colour: three syntaxes, one value. */
    ROBO_CHECK_EQ_INT((int)css_color_parse("c: hsl(120, 100%, 25%)", &color),
                      (int)ROBO_CSS_COLOR_PARSE_RESOLVED);
    {
        robo_css_color_t green = robo_css_color_none();

        ROBO_CHECK_EQ_INT(robo_css_color_from_name(robo_css_slice_from_text("green"),
                                                   &green),
                          1);
        ROBO_CHECK(robo_css_color_equals(color, green));
    }
    css_color_same("c: hsl(0, 100%, 50%)", "c: #ff0000");
    css_color_same("c: hsl(180, 100%, 50%)", "c: #00ffff");
    /* 210° at full saturation, 20% lightness: the blue channel is lightness plus
     * half the chroma, so 102, and the green is the sector's intermediate — 51,
     * not 68. */
    css_color_same("c: hsl(210, 100%, 20%)", "c: #003366");
    css_color_same("c: hsl(210, 100%, 20%)", "c: rgb(0, 51, 102)");

    /* Alpha: the legacy fourth argument and the modern slash, numeric or
     * percentage, and `hsla` is the same function. */
    css_color_is("c: hsla(0, 100%, 50%, 0.5)", "#ff000080");
    css_color_is("c: hsla(0, 100%, 50%, 50%)", "#ff000080");
    css_color_is("c: hsl(0 100% 50% / 50%)", "#ff000080");
    css_color_is("c: hsl(0, 100%, 50%, 0)", "#ff000000");
    css_color_is("c: hsl(0 100% 50%)", "#ff0000");

    /* The modern space syntax, and whitespace where it is legal. */
    css_color_is("c: hsl( 0   100%   50% )", "#ff0000");
    css_color_is("c: hsl( 0 , 100% , 50% )", "#ff0000");
    css_color_same("c: hsl(120 50 50)", "c: hsl(120, 50%, 50%)");

    /* Hue is normalised, not clamped: a wheel can be written either way round,
     * and one and a half turns is half a turn. */
    css_color_same("c: hsl(-120, 100%, 50%)", "c: hsl(240, 100%, 50%)");
    css_color_same("c: hsl(-240, 100%, 50%)", "c: hsl(120, 100%, 50%)");
    css_color_same("c: hsl(480, 100%, 50%)", "c: hsl(120, 100%, 50%)");
    css_color_same("c: hsl(720, 100%, 50%)", "c: hsl(0, 100%, 50%)");
    css_color_same("c: hsl(360, 100%, 50%)", "c: hsl(0, 100%, 50%)");
    css_color_same("c: hsl(-360, 100%, 50%)", "c: hsl(0, 100%, 50%)");

    /* An angle is a number with one of the four angle units. */
    css_color_same("c: hsl(0.5turn, 100%, 50%)", "c: hsl(180deg, 100%, 50%)");
    css_color_same("c: hsl(200grad, 100%, 50%)", "c: hsl(180deg, 100%, 50%)");
    css_color_same("c: hsl(3.141592653589793rad, 100%, 50%)", "c: hsl(180deg, 100%, 50%)");
    css_color_is("c: hsl(0.5turn 100% 50%)", "#00ffff");
    css_color_same("c: hsl(1.25turn, 100%, 50%)", "c: hsl(90, 100%, 50%)");

    /* Saturation and lightness clamp rather than wrapping, which is what makes a
     * negative saturation grey instead of a colour nobody asked for. */
    css_color_same("c: hsl(0, 150%, 50%)", "c: hsl(0, 100%, 50%)");
    css_color_same("c: hsl(0, -10%, 50%)", "c: hsl(0, 0%, 50%)");
    css_color_is("c: hsl(0, 0%, 50%)", "#808080");
    css_color_same("c: hsl(0, 100%, 150%)", "c: hsl(0, 100%, 100%)");
    css_color_same("c: hsl(0, 100%, -10%)", "c: hsl(0, 100%, 0%)");

    /* An angle this large has no fractional part left in a double, and the bound
     * is documented and pinned here rather than left to the platform: the hue
     * saturates to zero instead of producing an artefact. */
    css_color_same("c: hsl(1e300turn, 100%, 50%)", "c: hsl(0, 100%, 50%)");

    /* Malformed: nothing, too few, too many, a unit where a saturation belongs,
     * the wrong separator, a length or a percentage used as a hue, and the
     * legacy comma form written with bare numbers (CSS requires percentages
     * there, and the modern form is where numbers are read as percentages). */
    css_color_outcome_is("c: hsl()", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(1)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(0, 100%)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(0 100%)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(0, 100%, 50%, 0.5, 2)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(0, 100%, 50% / 0.5)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(0, 50, 50)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(10px 50% 50%)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(0, 10px, 50%)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(10% 50% 50%)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(0 100% 50% 0.5)", ROBO_CSS_COLOR_PARSE_MALFORMED);
    css_color_outcome_is("c: hsl(1e999, 100%, 50%)", ROBO_CSS_COLOR_PARSE_MALFORMED);
}

/* ------------------------------------------------------------------ */
/* currentColor, and the colour spaces this build defers to            */
/* ------------------------------------------------------------------ */

static void test_color_current_color(void)
{
    robo_css_color_t color = robo_css_color_none();
    robo_css_color_t resolved = robo_css_color_none();

    css_fixture();

    /* Typed, not resolved: the keyword keeps its spelling and the record says it
     * is contextual rather than pretending to be a channel set. */
    css_color_outcome_is("c: currentColor", ROBO_CSS_COLOR_PARSE_CURRENT);
    ROBO_CHECK_EQ_INT((int)css_color_parse("c: currentColor", &color),
                      (int)ROBO_CSS_COLOR_PARSE_CURRENT);
    ROBO_CHECK(robo_css_color_is_current(color));
    ROBO_CHECK(!robo_css_color_is_resolved(color));
    ROBO_CHECK(robo_css_color_is_valid(color));
    ROBO_CHECK_EQ_INT(color.kind, ROBO_CSS_COLOR_CURRENT);
    css_color_written_is(color, "currentColor");

    /* One keyword, so its spelling is not part of the value. */
    css_color_outcome_is("c: CURRENTCOLOR", ROBO_CSS_COLOR_PARSE_CURRENT);
    css_color_outcome_is("c: currentcolor", ROBO_CSS_COLOR_PARSE_CURRENT);
    {
        robo_css_color_t upper = robo_css_color_none();

        ROBO_CHECK_EQ_INT((int)css_color_parse("c: CURRENTCOLOR", &upper),
                          (int)ROBO_CSS_COLOR_PARSE_CURRENT);
        ROBO_CHECK(robo_css_color_equals(upper, color));
    }

    /* The substitution itself, and the four cases. */
    ROBO_CHECK_EQ_INT(robo_css_color_resolve_current(
                          robo_css_color_make(1u, 2u, 3u, 255u),
                          robo_css_color_current(), &resolved),
                      1);
    css_color_written_is(resolved, "#010203");
    ROBO_CHECK_EQ_INT(robo_css_color_resolve_current(
                          robo_css_color_current(), robo_css_color_make(0x33u, 0x66u, 0x99u, 0x80u),
                          &resolved),
                      1);
    css_color_written_is(resolved, "#33669980");
    /* Nothing to substitute: staying contextual is the answer, and inventing
     * black would be the mistake the keyword exists to prevent. */
    ROBO_CHECK_EQ_INT(robo_css_color_resolve_current(robo_css_color_current(),
                                                     robo_css_color_current(), &resolved),
                      0);
    ROBO_CHECK(robo_css_color_is_current(resolved));
    ROBO_CHECK_EQ_INT(robo_css_color_resolve_current(robo_css_color_current(),
                                                     robo_css_color_none(), &resolved),
                      0);
    ROBO_CHECK(robo_css_color_is_current(resolved));
    /* A non-colour is not a colour, and resolving does not make it one. */
    ROBO_CHECK_EQ_INT(robo_css_color_resolve_current(robo_css_color_invalid(),
                                                     robo_css_color_make(1u, 2u, 3u, 255u),
                                                     &resolved),
                      0);
    ROBO_CHECK_EQ_INT(resolved.kind, ROBO_CSS_COLOR_INVALID);
    ROBO_CHECK_EQ_INT(robo_css_color_resolve_current(robo_css_color_none(),
                                                     robo_css_color_make(1u, 2u, 3u, 255u),
                                                     &resolved),
                      0);
    ROBO_CHECK_EQ_INT(resolved.kind, ROBO_CSS_COLOR_NONE);
    /* The output is optional: the verdict is the return value. */
    ROBO_CHECK_EQ_INT(robo_css_color_resolve_current(robo_css_color_current(),
                                                     robo_css_color_make(1u, 2u, 3u, 255u),
                                                     NULL),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_color_resolve_current(robo_css_color_current(),
                                                     robo_css_color_current(), NULL),
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
static void test_color_deferred_spaces(void)
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

    css_fixture();
    for (index = 0u; index < sizeof(deferred) / sizeof(deferred[0]); index++) {
        char declaration[64];

        (void)snprintf(declaration, sizeof(declaration), "c: %s", deferred[index]);
        css_color_outcome_is(declaration, ROBO_CSS_COLOR_PARSE_DEFERRED);
    }
    for (index = 0u; index < sizeof(unrepresentable) / sizeof(unrepresentable[0]);
         index++) {
        ROBO_CHECK_EQ_INT(robo_css_color_function_is_unrepresentable(
                              robo_css_slice_from_text(unrepresentable[index])),
                          1);
        ROBO_CHECK_EQ_INT(robo_css_color_function_is_unrepresentable(
                              robo_css_slice_from_text("rgb")),
                          0);
    }
    /* Case-insensitive, as function names are. */
    ROBO_CHECK_EQ_INT(robo_css_color_function_is_unrepresentable(
                          robo_css_slice_from_text("OKLCH")),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_color_function_is_unrepresentable(robo_css_slice_empty()), 0);
    ROBO_CHECK_EQ_INT(robo_css_color_function_is_unrepresentable(
                          robo_css_slice_from_text("")),
                      0);

    /* A function nobody has heard of is not a colour, and neither is an
     * identifier that is not a colour keyword. */
    css_color_outcome_is("c: sparkle(1)", ROBO_CSS_COLOR_PARSE_INVALID);
    css_color_outcome_is("c: nonsense", ROBO_CSS_COLOR_PARSE_UNKNOWN_NAME);
    css_color_outcome_is("c: reddish", ROBO_CSS_COLOR_PARSE_UNKNOWN_NAME);
}

/* ------------------------------------------------------------------ */
/* Shapes that are not colours                                        */
/* ------------------------------------------------------------------ */

static void test_color_not_a_colour(void)
{
    robo_css_color_t color = robo_css_color_none();

    css_fixture();

    /* Two colours is not a colour, and neither is a number, a string or a
     * dimension — each of them is its own kind of wrong. */
    css_color_outcome_is("c: red blue", ROBO_CSS_COLOR_PARSE_INVALID);
    css_color_outcome_is("c: 42", ROBO_CSS_COLOR_PARSE_INVALID);
    css_color_outcome_is("c: 10px", ROBO_CSS_COLOR_PARSE_INVALID);
    css_color_outcome_is("c: \"red\"", ROBO_CSS_COLOR_PARSE_INVALID);
    css_color_outcome_is("c: #ff0000 #00ff00", ROBO_CSS_COLOR_PARSE_INVALID);
    css_color_outcome_is("c: none", ROBO_CSS_COLOR_PARSE_UNKNOWN_NAME);
    /* `transparent` is a colour; the keyword is answered where the ident is. */
    css_color_outcome_is("c: transparent", ROBO_CSS_COLOR_PARSE_RESOLVED);

    /* A value with nothing in it is not a colour, and neither is a NULL output:
     * both are answered rather than dereferenced. */
    {
        robo_css_value_t empty = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT((int)robo_css_color_from_value(&empty, &color),
                          (int)ROBO_CSS_COLOR_PARSE_INVALID);
        ROBO_CHECK_EQ_INT((int)robo_css_color_from_value(NULL, &color),
                          (int)ROBO_CSS_COLOR_PARSE_INVALID);
        ROBO_CHECK_EQ_INT((int)robo_css_color_from_value(&empty, NULL),
                          (int)ROBO_CSS_COLOR_PARSE_INVALID);
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
static void test_color_robust(void)
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

    css_fixture();
    for (index = 0u; index < sizeof(declarations) / sizeof(declarations[0]); index++) {
        robo_css_color_t color = robo_css_color_none();
        robo_css_color_parse_t outcome = css_color_parse(declarations[index], &color);

        ROBO_CHECK(robo_css_color_parse_is_known(outcome));
        ROBO_CHECK(robo_css_color_kind_is_known(color.kind));
        /* Resolved means a real colour: never a half-filled record. */
        if (outcome == ROBO_CSS_COLOR_PARSE_RESOLVED) {
            ROBO_CHECK(robo_css_color_is_resolved(color));
        }
        /* And a colour that was not produced is NONE or INVALID, never a
         * plausible-looking channel set nobody wrote. */
        if (outcome != ROBO_CSS_COLOR_PARSE_RESOLVED &&
            outcome != ROBO_CSS_COLOR_PARSE_CURRENT) {
            ROBO_CHECK(!robo_css_color_is_valid(color));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Serialization                                                       */
/* ------------------------------------------------------------------ */

static void test_color_write(void)
{
    char buffer[ROBO_CSS_COLOR_TEXT_MAX];
    size_t length = 123u;
    robo_css_diag_t diag;

    css_fixture();

    /* The four kinds, one spelling each. NONE and INVALID are not CSS and are
     * written as the diagnostics they are: this is a diagnostic rendering, not a
     * CSSOM serialization. */
    css_color_written_is(robo_css_color_none(), "none");
    css_color_written_is(robo_css_color_invalid(), "invalid");
    css_color_written_is(robo_css_color_current(), "currentColor");
    css_color_written_is(robo_css_color_make(0u, 0u, 0u, 255u), "#000000");
    css_color_written_is(robo_css_color_make(255u, 255u, 255u, 255u), "#ffffff");
    /* Alpha appears only when it is not opaque, and lowercase hex either way. */
    css_color_written_is(robo_css_color_make(0xABu, 0xCDu, 0xEFu, 255u), "#abcdef");
    css_color_written_is(robo_css_color_make(0xABu, 0xCDu, 0xEFu, 0x80u), "#abcdef80");
    css_color_written_is(robo_css_color_make(0u, 0u, 0u, 0u), "#00000000");

    /* A NULL argument, an unknown kind and too little room are all reported
     * rather than written past. */
    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_color_write(robo_css_color_current(), NULL, 8u, &length,
                                           &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_color_write(robo_css_color_current(), buffer, sizeof(buffer),
                                           NULL, &diag),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_css_color_t cleared = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_color_write(cleared, buffer, sizeof(buffer), &length,
                                               &diag),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buffer, "none");
    }
    {
        robo_css_color_t unknown = robo_css_color_make(1u, 2u, 3u, 255u);

        unknown.kind = (robo_css_color_kind_t)99;
        length = 0u;
        ROBO_CHECK_EQ_INT(robo_css_color_write(unknown, buffer, sizeof(buffer), &length,
                                               &diag),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(length, 0u);
        ROBO_CHECK_EQ_INT(buffer[0], '\0');
    }
    /* A LONGER spelling than the buffer holds is refused, and the caller is told
     * how much room it would have needed — no truncation, and no half a colour. */
    length = 0u;
    ROBO_CHECK_EQ_INT(robo_css_color_write(robo_css_color_make(1u, 2u, 3u, 0x80u), buffer,
                                           5u, &length, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(length, 9u);
    ROBO_CHECK_EQ_INT(buffer[0], '\0');
}

/* ------------------------------------------------------------------ */
/* Integration: colours through the pipeline                           */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style through the real context door, asserting success. */
static void css_color_compute_of(css_inherit_fixture_t *fixture,
                                 robo_css_element_ref_t ref,
                                 robo_css_computed_style_t *out)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(css_inherit_context(fixture), ref,
                                                          out, &error, NULL),
                      ROBO_OK);
    /* A colour declaration this suite writes is legal, so a clean document is
     * what every expectation below is an expectation about. */
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
}

/** The kind of @p name's computed value, or NONE when the entry is absent. */
static robo_css_computed_value_kind_t css_color_computed_kind(
    const robo_css_computed_style_t *style, const char *name)
{
    const robo_css_computed_value_t *value =
        robo_css_computed_style_computed_by_name(style, robo_css_slice_from_text(name));

    return value ? value->kind : ROBO_CSS_COMPUTED_VALUE_NONE;
}

/**
 * Every ordinary spelling, computed.
 *
 * One sheet, one element per form, and the assertion is the *computed* text — so
 * what this proves is that the four syntaxes converge on one representation
 * rather than that each parser can be made to return something.
 */
static void test_color_computed_declared(void)
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
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t element[sizeof(forms) / sizeof(forms[0])];
    char sheet[1024];
    size_t offset = 0u;
    size_t index;

    css_fixture();
    css_inherit_begin(&fixture);
    sheet[0] = '\0';
    for (index = 0u; index < sizeof(forms) / sizeof(forms[0]); index++) {
        element[index] = css_host_add(css_inherit_host(&fixture), "div", forms[index].id,
                                      NULL);
        offset += (size_t)snprintf(sheet + offset, sizeof(sheet) - offset, "#%s { color: %s }",
                                   forms[index].id, forms[index].declared);
    }
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, sheet);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    for (index = 0u; index < sizeof(forms) / sizeof(forms[0]); index++) {
        css_color_compute_of(&fixture, element[index], style);
        css_computed_is(style, "color", forms[index].computed);
        css_computed_flags_are(style, "color", "declared");
        ROBO_CHECK_EQ_INT(css_color_computed_kind(style, "color"),
                          ROBO_CSS_COMPUTED_VALUE_COLOR);
    }

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * A colour that inherits, and one that is overridden on the way down.
 *
 * `background-color` is here because it is the one other property whose value is
 * a colour: it must NOT inherit, and a test that only looked at `color` would
 * pass whether or not the distinction existed.
 */
static void test_color_computed_inheritance(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *outer = NULL;
    robo_css_computed_style_t *middle = NULL;
    robo_css_computed_style_t *inner = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t section;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "app", NULL);
    section = css_host_add(css_inherit_host(&fixture), "section", NULL, NULL);
    paragraph = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), section, div);
    css_host_set_parent(css_inherit_host(&fixture), paragraph, section);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#app { color: #336699; background-color: #00ff00 }"
                      "section { color: rgba(255, 0, 0, 0.5) }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&outer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&middle, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&inner, NULL), ROBO_OK);

    css_color_compute_of(&fixture, div, outer);
    css_color_compute_of(&fixture, section, middle);
    css_color_compute_of(&fixture, paragraph, inner);

    css_computed_is(outer, "color", "#336699");
    css_computed_flags_are(outer, "color", "declared");
    /* An inherited colour arrives as the parent's *typed* colour, not as a
     * re-parsed spelling: the two are distinguishable because `rgba(..., 0.5)`
     * is 128/255 and only the pipeline that computed it knows that. */
    css_computed_is(middle, "color", "#ff000080");
    css_computed_flags_are(middle, "color", "declared");
    ROBO_CHECK_EQ_INT(css_color_computed_kind(middle, "color"),
                      ROBO_CSS_COMPUTED_VALUE_COLOR);

    /* The paragraph declared nothing, so it inherits from its *immediate*
     * parent — the half-transparent red and not the grandparent's blue-ish. */
    css_computed_is(inner, "color", "#ff000080");
    css_computed_flags_are(inner, "color", "inherited");
    ROBO_CHECK_EQ_INT(css_color_computed_kind(inner, "color"),
                      ROBO_CSS_COMPUTED_VALUE_COLOR);

    /* `background-color` is declared on the outermost element and inherited by
     * nobody. */
    css_computed_is(outer, "background-color", "#00ff00");
    css_computed_flags_are(outer, "background-color", "declared");
    css_computed_is(inner, "background-color", "<absent>");

    robo_css_computed_style_destroy(inner);
    robo_css_computed_style_destroy(middle);
    robo_css_computed_style_destroy(outer);
    css_inherit_end(&fixture);
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
static void test_color_computed_wide_keywords(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent = NULL;
    robo_css_computed_style_t *initial_style = NULL;
    robo_css_computed_style_t *inherit_style = NULL;
    robo_css_computed_style_t *unset_style = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t initial_child;
    robo_css_element_ref_t inherit_child;
    robo_css_element_ref_t unset_child;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    initial_child = css_host_add(css_inherit_host(&fixture), "p", "a", NULL);
    inherit_child = css_host_add(css_inherit_host(&fixture), "p", "b", NULL);
    unset_child = css_host_add(css_inherit_host(&fixture), "p", "c", NULL);
    css_host_set_parent(css_inherit_host(&fixture), initial_child, div);
    css_host_set_parent(css_inherit_host(&fixture), inherit_child, div);
    css_host_set_parent(css_inherit_host(&fixture), unset_child, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: #336699; background-color: #00ff00 }"
                      "#a { color: initial; background-color: initial }"
                      "#b { color: inherit; background-color: inherit }"
                      "#c { color: unset; background-color: unset }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&initial_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&inherit_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&unset_style, NULL), ROBO_OK);

    css_color_compute_of(&fixture, div, parent);
    css_color_compute_of(&fixture, initial_child, initial_style);
    css_color_compute_of(&fixture, inherit_child, inherit_style);
    css_color_compute_of(&fixture, unset_child, unset_style);

    /* The registry's initial colour for `color`, and still a typed colour. */
    css_computed_is(initial_style, "color", "#000000");
    css_computed_flags_are(initial_style, "color", "default");
    ROBO_CHECK_EQ_INT(css_color_computed_kind(initial_style, "color"),
                      ROBO_CSS_COMPUTED_VALUE_COLOR);
    /* `background-color`'s initial value is `transparent`, which is also a
     * colour — and the empty one, so the alpha is the assertion. */
    css_computed_is(initial_style, "background-color", "#00000000");
    css_computed_flags_are(initial_style, "background-color", "default");

    /* `inherit` pulls the parent's computed colour for either kind — including
     * the one that does not inherit by default, which is the whole reason the
     * keyword exists. */
    css_computed_is(inherit_style, "color", "#336699");
    css_computed_flags_are(inherit_style, "color", "inherited");
    css_computed_is(inherit_style, "background-color", "#00ff00");
    css_computed_flags_are(inherit_style, "background-color", "inherited");

    /* `unset` is `inherit` on `color` and `initial` on `background-color` — one
     * declaration, two different answers. */
    css_computed_is(unset_style, "color", "#336699");
    css_computed_flags_are(unset_style, "color", "inherited");
    css_computed_is(unset_style, "background-color", "#00000000");
    css_computed_flags_are(unset_style, "background-color", "default");

    robo_css_computed_style_destroy(unset_style);
    robo_css_computed_style_destroy(inherit_style);
    robo_css_computed_style_destroy(initial_style);
    robo_css_computed_style_destroy(parent);
    css_inherit_end(&fixture);
}

/**
 * The two `currentColor` rules, and the case where neither can apply.
 *
 * `color: currentColor` is the colour the element would have *inherited*, and
 * `background-color: currentColor` is the element's *own* colour. The two
 * children below separate them: if the implementation used one rule for both,
 * exactly one of the four assertions would fail.
 */
static void test_color_computed_current_color_rules(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent = NULL;
    robo_css_computed_style_t *same = NULL;
    robo_css_computed_style_t *own = NULL;
    robo_css_computed_style_t *root = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t same_child;
    robo_css_element_ref_t own_child;
    robo_css_element_ref_t root_element;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "app", NULL);
    same_child = css_host_add(css_inherit_host(&fixture), "p", "a", NULL);
    own_child = css_host_add(css_inherit_host(&fixture), "p", "b", NULL);
    root_element = css_host_add(css_inherit_host(&fixture), "html", "r", NULL);
    css_host_set_parent(css_inherit_host(&fixture), same_child, div);
    css_host_set_parent(css_inherit_host(&fixture), own_child, div);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#app { color: #336699 }"
                      "#a { color: currentColor; background-color: currentColor }"
                      "#b { color: #ff0000; background-color: currentColor }"
                      "#r { color: currentColor; background-color: currentColor }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&same, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&own, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&root, NULL), ROBO_OK);

    css_color_compute_of(&fixture, div, parent);
    css_color_compute_of(&fixture, same_child, same);
    css_color_compute_of(&fixture, own_child, own);
    css_color_compute_of(&fixture, root_element, root);

    /* Rule one: on `color`, `currentColor` is the parent's computed colour, so
     * the child ends up the same colour rather than a fixed value. */
    css_computed_is(same, "color", "#336699");
    css_computed_flags_are(same, "color", "declared");
    ROBO_CHECK_EQ_INT(css_color_computed_kind(same, "color"),
                      ROBO_CSS_COMPUTED_VALUE_COLOR);
    /* Rule two: everywhere else it is the element's own colour, which here is
     * the same one — so this alone would not distinguish the two rules. */
    css_computed_is(same, "background-color", "#336699");
    ROBO_CHECK_EQ_INT(css_color_computed_kind(same, "background-color"),
                      ROBO_CSS_COMPUTED_VALUE_COLOR);

    /* This is the pair that separates them: the element's own colour is red
     * while its parent's is not, and `background-color` must follow the element. */
    css_computed_is(own, "color", "#ff0000");
    css_computed_is(own, "background-color", "#ff0000");

    /* Nothing to resolve against: a root element has no parent computed style,
     * so `currentColor` stays contextual and stays flagged. Filling in black
     * would be exactly the mistake the keyword exists to prevent. */
    css_computed_is(root, "color", "currentColor");
    css_computed_flags_are(root, "color", "declared+unresolved");
    ROBO_CHECK_EQ_INT(css_color_computed_kind(root, "color"),
                      ROBO_CSS_COMPUTED_VALUE_COLOR);
    css_computed_is(root, "background-color", "currentColor");
    css_computed_flags_are(root, "background-color", "declared+unresolved");

    robo_css_computed_style_destroy(root);
    robo_css_computed_style_destroy(own);
    robo_css_computed_style_destroy(same);
    robo_css_computed_style_destroy(parent);
    css_inherit_end(&fixture);
}

/**
 * A reference, a misspelling, and a custom property — three different answers.
 *
 * `color: var(--brand)` is legal CSS that this build cannot resolve yet, so it is
 * carried rather than rejected; `background-color: potato` is a misspelling and is
 * reported invalid; `--brand` itself is never *interpreted*, so a colour written
 * into a custom property stays the tokens it was written as.
 */
static void test_color_computed_reference_and_invalid(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { --brand: #336699; color: var(--brand, red);"
                      " background-color: potato; display: block }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    css_color_compute_of(&fixture, div, style);

    /* The reference is carried with its fallback intact, and is *not* a colour.
     * Nothing substitutes `--brand`, which is the promise custom properties are
     * held to. */
    css_computed_is(style, "color", "var(--brand, red)");
    css_computed_flags_are(style, "color", "declared+unresolved");
    ROBO_CHECK_EQ_INT(css_color_computed_kind(style, "color"),
                      ROBO_CSS_COMPUTED_VALUE_PRESERVED);

    /* The custom property keeps the tokens it was written as — a colour written
     * into one is not eagerly parsed into a colour. */
    css_computed_is(style, "--brand", "#336699");
    css_computed_flags_are(style, "--brand", "declared+custom");
    ROBO_CHECK_EQ_INT(css_color_computed_kind(style, "--brand"),
                      ROBO_CSS_COMPUTED_VALUE_PRESERVED);

    /* A misspelling is a different fact from a reference: the declaration was
     * there and was wrong, and a consumer can see which. */
    css_computed_flags_are(style, "background-color", "declared+invalid");
    ROBO_CHECK_EQ_INT(css_color_computed_kind(style, "background-color"),
                      ROBO_CSS_COMPUTED_VALUE_INVALID);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(style), 1);
    /* ...and it does not corrupt its neighbours. */
    css_computed_is(style, "display", "block");
    css_computed_flags_are(style, "display", "declared");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * The whole pipeline over a three-level tree.
 *
 * selector matching → specificity → cascade → inheritance → computed, with the
 * `color` on each level decided by a *different* kind of selector, so a break in
 * any one stage changes a colour that a later assertion reads.
 */
static void test_color_computed_selector_pipeline(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *app_style = NULL;
    robo_css_computed_style_t *panel_style = NULL;
    robo_css_computed_style_t *title_style = NULL;
    robo_css_computed_style_t *plain_style = NULL;
    robo_css_element_ref_t app;
    robo_css_element_ref_t panel;
    robo_css_element_ref_t title;
    robo_css_element_ref_t plain;

    css_fixture();
    css_inherit_begin(&fixture);
    app = css_host_add(css_inherit_host(&fixture), "div", "app", NULL);
    panel = css_host_add(css_inherit_host(&fixture), "section", NULL, "panel");
    title = css_host_add(css_inherit_host(&fixture), "p", NULL, "title");
    plain = css_host_add(css_inherit_host(&fixture), "p", NULL, NULL);
    css_host_set_parent(css_inherit_host(&fixture), panel, app);
    css_host_set_parent(css_inherit_host(&fixture), title, panel);
    css_host_set_parent(css_inherit_host(&fixture), plain, panel);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#app { color: #336699 }"
                      ".panel { color: #00ff00 }"
                      ".title { color: #ff0000 }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&app_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&panel_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&title_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&plain_style, NULL), ROBO_OK);

    css_color_compute_of(&fixture, app, app_style);
    css_color_compute_of(&fixture, panel, panel_style);
    css_color_compute_of(&fixture, title, title_style);
    css_color_compute_of(&fixture, plain, plain_style);

    /* An id beats a class beats nothing, and each element's own declaration
     * reaches it as a typed colour. */
    css_computed_is(app_style, "color", "#336699");
    css_computed_flags_are(app_style, "color", "declared");
    css_computed_is(panel_style, "color", "#00ff00");
    css_computed_flags_are(panel_style, "color", "declared");
    css_computed_is(title_style, "color", "#ff0000");
    css_computed_flags_are(title_style, "color", "declared");
    /* The one element nobody selected inherits its parent's computed colour. */
    css_computed_is(plain_style, "color", "#00ff00");
    css_computed_flags_are(plain_style, "color", "inherited");

    robo_css_computed_style_destroy(plain_style);
    robo_css_computed_style_destroy(title_style);
    robo_css_computed_style_destroy(panel_style);
    robo_css_computed_style_destroy(app_style);
    css_inherit_end(&fixture);
}

/**
 * The same element computed twice produces the same colour, twice.
 *
 * Determinism is the property a later phase will depend on — a cached or
 * reordered style that spelled a colour differently the second time would make
 * every repaint a possible flicker — so it is asserted rather than assumed.
 */
static void test_color_computed_determinism(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *first = NULL;
    robo_css_computed_style_t *second = NULL;
    robo_css_element_ref_t div;
    char left[ROBO_CSS_COLOR_TEXT_MAX];
    char right[ROBO_CSS_COLOR_TEXT_MAX];
    size_t left_length = 0u;
    size_t right_length = 0u;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", NULL, NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: hsl(210, 50%, 40%); background-color: #f008 }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&first, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&second, NULL), ROBO_OK);

    css_color_compute_of(&fixture, div, first);
    css_color_compute_of(&fixture, div, second);

    /* Compared through the typed colour rather than through the text, because
     * the record is the thing that has to be stable. */
    {
        const robo_css_computed_value_t *a = robo_css_computed_style_computed_by_name(
            first, robo_css_slice_from_text("color"));
        const robo_css_computed_value_t *b = robo_css_computed_style_computed_by_name(
            second, robo_css_slice_from_text("color"));

        ROBO_CHECK(a != NULL && b != NULL);
        if (a && b) {
            css_color_equals_message(a->color, b->color, "color computed twice");
            ROBO_CHECK_EQ_INT(a->unresolved, b->unresolved);
        }
    }
    css_computed_is(first, "color", "#336699");
    css_computed_is(second, "color", "#336699");
    ROBO_CHECK_EQ_INT(robo_css_color_write(
                          robo_css_computed_style_computed_by_name(
                              first, robo_css_slice_from_text("background-color"))->color,
                          left, sizeof(left), &left_length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_color_write(
                          robo_css_computed_style_computed_by_name(
                              second, robo_css_slice_from_text("background-color"))->color,
                          right, sizeof(right), &right_length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(left, right);
    ROBO_CHECK_EQ_INT(left_length, right_length);
    css_computed_is(first, "background-color", "#ff000088");

    robo_css_computed_style_destroy(second);
    robo_css_computed_style_destroy(first);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_color(void);

void robo_test_register_css_color(void)
{
    robo_test_register("css.color.records", test_color_records);
    robo_test_register("css.color.parse_outcomes", test_color_parse_outcomes);
    robo_test_register("css.color.named_representative", test_color_named_representative);
    robo_test_register("css.color.named_table", test_color_named_table);
    robo_test_register("css.color.hex", test_color_hex);
    robo_test_register("css.color.rgb", test_color_rgb);
    robo_test_register("css.color.hsl", test_color_hsl);
    robo_test_register("css.color.current_color", test_color_current_color);
    robo_test_register("css.color.deferred_spaces", test_color_deferred_spaces);
    robo_test_register("css.color.not_a_colour", test_color_not_a_colour);
    robo_test_register("css.color.robust", test_color_robust);
    robo_test_register("css.color.write", test_color_write);
    robo_test_register("css.color.computed.declared", test_color_computed_declared);
    robo_test_register("css.color.computed.inheritance", test_color_computed_inheritance);
    robo_test_register("css.color.computed.wide_keywords", test_color_computed_wide_keywords);
    robo_test_register("css.color.computed.current_color_rules",
                       test_color_computed_current_color_rules);
    robo_test_register("css.color.computed.reference_and_invalid",
                       test_color_computed_reference_and_invalid);
    robo_test_register("css.color.computed.selector_pipeline",
                       test_color_computed_selector_pipeline);
    robo_test_register("css.color.computed.determinism", test_color_computed_determinism);
}
