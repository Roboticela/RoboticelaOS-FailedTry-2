/*
 * RoboCSS computed values (Phase 9.8) — see RoboCSSComputedValue.h.
 *
 * Three things live here and nothing else:
 *
 *   units      the closed set, which of them have a fixed px factor, and the
 *              identifier -> unit mapping
 *   colours    the standard named set, the four hex lengths, and rgb()/rgba()
 *              in both syntaxes — everything the COLOUR kind needs, with the
 *              modern spaces and `currentColor` left to 9.9
 *   compute    the one dispatch from "what shape is this property" to a typed
 *              value, which is the whole of this phase's normalisation
 *
 * Two habits are worth naming because they are what keeps the file honest.
 *
 * First, every path that reads a token reads its *value* rather than its `text`:
 * `R\47 B` is `rgb`, `\72 ed` is `red`, and a hex digit cannot be an escape
 * because escapes were already resolved by the time this file looks. Using the
 * raw bytes would make an escape spelling a different colour, which is exactly
 * the bug CSS's escape rule exists to prevent.
 *
 * Second, nothing here resolves a relative length or a percentage. `2em` is
 * carried as 2 em and `50%` as 50 percent because the information needed to go
 * further — a parent font size, a containing block — does not exist at this
 * stage. A converter that guessed would produce a number that looked computed
 * and was wrong.
 */

#include "RoboCSS/RoboCSSComputedValue.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Units                                                              */
/* ------------------------------------------------------------------ */

/** One unit: its spelling, and its factor to px (0 when it has none). */
typedef struct computed_unit_entry {
    const char *name;
    robo_css_unit_t unit;
    double to_px;
} computed_unit_entry_t;

/*
 * The closed set.
 *
 * The px factors are CSS's own, and they are exact by definition rather than by
 * measurement: 1in is 96px, 1cm is 96/2.54px, and the rest follow. Nothing here
 * consults a display's DPI, which is the point — an absolute length means the
 * same thing everywhere, and only a *used* value (a later phase's) may care
 * about the device.
 */
static const computed_unit_entry_t computed_units[] = {
    { "px",   ROBO_CSS_UNIT_PX,   1.0 },
    { "cm",   ROBO_CSS_UNIT_CM,   96.0 / 2.54 },
    { "mm",   ROBO_CSS_UNIT_MM,   96.0 / 25.4 },
    { "q",    ROBO_CSS_UNIT_Q,    96.0 / 101.6 },
    { "in",   ROBO_CSS_UNIT_IN,   96.0 },
    { "pt",   ROBO_CSS_UNIT_PT,   96.0 / 72.0 },
    { "pc",   ROBO_CSS_UNIT_PC,   16.0 },
    { "em",   ROBO_CSS_UNIT_EM,   0.0 },
    { "rem",  ROBO_CSS_UNIT_REM,  0.0 },
    { "ex",   ROBO_CSS_UNIT_EX,   0.0 },
    { "ch",   ROBO_CSS_UNIT_CH,   0.0 },
    { "vw",   ROBO_CSS_UNIT_VW,   0.0 },
    { "vh",   ROBO_CSS_UNIT_VH,   0.0 },
    { "vmin", ROBO_CSS_UNIT_VMIN, 0.0 },
    { "vmax", ROBO_CSS_UNIT_VMAX, 0.0 }
};

int robo_css_unit_is_known(robo_css_unit_t unit)
{
    return unit >= ROBO_CSS_UNIT_NONE && unit < (robo_css_unit_t)ROBO_CSS_UNIT_COUNT;
}

int robo_css_unit_is_absolute(robo_css_unit_t unit)
{
    size_t index;

    /* Asked of the table, not of `to_px`: that function answers with the value
     * it was given when the unit has no fixed factor, so a relative unit and an
     * undeclared one both come back looking convertible. The factor being
     * present is the fact this predicate is about. */
    for (index = 0u; index < sizeof(computed_units) / sizeof(computed_units[0]); index++) {
        if (computed_units[index].unit == unit) {
            return computed_units[index].to_px != 0.0;
        }
    }
    return 0;
}

const char *robo_css_unit_name(robo_css_unit_t unit)
{
    size_t index;

    if (unit == ROBO_CSS_UNIT_NONE) {
        return "";
    }
    for (index = 0u; index < sizeof(computed_units) / sizeof(computed_units[0]); index++) {
        if (computed_units[index].unit == unit) {
            return computed_units[index].name;
        }
    }
    return "";
}

robo_css_unit_t robo_css_unit_from_slice(robo_css_slice_t name)
{
    size_t index;

    if (robo_css_slice_is_absent(name) || name.length == 0u) {
        return ROBO_CSS_UNIT_NONE;
    }
    for (index = 0u; index < sizeof(computed_units) / sizeof(computed_units[0]); index++) {
        if (robo_css_bytes_equal_ascii_ci(name.bytes, name.length,
                                         computed_units[index].name)) {
            return computed_units[index].unit;
        }
    }
    return ROBO_CSS_UNIT_NONE;
}

robo_css_unit_t robo_css_unit_from_text(const char *name)
{
    if (!name) {
        return ROBO_CSS_UNIT_NONE;
    }
    return robo_css_unit_from_slice(robo_css_slice_from_text(name));
}

double robo_css_unit_to_px(double value, robo_css_unit_t unit)
{
    size_t index;

    for (index = 0u; index < sizeof(computed_units) / sizeof(computed_units[0]); index++) {
        if (computed_units[index].unit == unit && computed_units[index].to_px != 0.0) {
            return value * computed_units[index].to_px;
        }
    }
    return value;
}

/* ------------------------------------------------------------------ */
/* Numbers                                                            */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p value can be a computed number at all.
 *
 * NaN and the infinities are refused rather than clamped or dropped quietly: a
 * computed value is a *representation of a CSS value*, and there is no CSS
 * spelling for either. A caller that constructs one has a bug, and the
 * normaliser reports it as the invalid value it is.
 *
 * The test allocates nothing and needs no math header: NaN is the only double
 * not equal to itself, and an infinity is the only one whose difference from
 * itself is not zero.
 */
static int computed_number_is_finite(double value)
{
    return value == value && (value - value) == 0.0;
}

/** @p value clamped into [@p low, @p high]. */
static double computed_clamp(double value, double low, double high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

/** @p value rounded to the nearest integer, away from zero on a half. */
static int computed_round(double value)
{
    return (int)(value < 0.0 ? value - 0.5 : value + 0.5);
}

/** 0–255 channel for a 0–255 number, clamped and rounded. */
static uint8_t computed_channel_255(double value)
{
    return (uint8_t)computed_round(computed_clamp(value, 0.0, 255.0));
}

/** 0–255 channel for a 0–100 percentage, clamped and rounded. */
static uint8_t computed_channel_percent(double value)
{
    return (uint8_t)computed_round(computed_clamp(value, 0.0, 100.0) * 255.0 / 100.0);
}

/** 0–255 alpha for a 0–1 number, clamped and rounded. */
static uint8_t computed_alpha_number(double value)
{
    return (uint8_t)computed_round(computed_clamp(value, 0.0, 1.0) * 255.0);
}

/** 0–255 alpha for a 0–100 percentage, clamped and rounded. */
static uint8_t computed_alpha_percent(double value)
{
    return (uint8_t)computed_round(computed_clamp(value, 0.0, 100.0) * 255.0 / 100.0);
}

/* ------------------------------------------------------------------ */
/* Colours                                                            */
/* ------------------------------------------------------------------ */

robo_css_color_t robo_css_color_make(uint8_t red, uint8_t green, uint8_t blue,
                                     uint8_t alpha)
{
    robo_css_color_t color;

    color.red = red;
    color.green = green;
    color.blue = blue;
    color.alpha = alpha;
    return color;
}

int robo_css_color_equals(robo_css_color_t a, robo_css_color_t b)
{
    return a.red == b.red && a.green == b.green && a.blue == b.blue && a.alpha == b.alpha;
}

/** One named colour, as a 24-bit RGB triple. */
typedef struct computed_named_color {
    const char *name;
    uint32_t rgb;
} computed_named_color_t;

/*
 * The standard named colours.
 *
 * Every name CSS defines, which is the 16 CSS2.1 keywords, the 130 colour names
 * SVG shares with X11, and `rebeccapurple`. A table this size is the reason the
 * search is a linear scan rather than something cleverer: 148 case-insensitive
 * string comparisons happen once per declaration at compute time, and an index
 * that could disagree with the table it indexes is a worse trade than a loop.
 *
 * `transparent` is *not* here. It is a colour (transparent black) but not a
 * named one, and it is handled where the identifier is, so that a reader looking
 * for the keyword does not have to know it is spelled as a table entry.
 */
static const computed_named_color_t computed_named_colors[] = {
    { "aliceblue", 0xf0f8ffu }, { "antiquewhite", 0xfaebd7u },
    { "aqua", 0x00ffffu }, { "aquamarine", 0x7fffd4u },
    { "azure", 0xf0ffffu }, { "beige", 0xf5f5dcu },
    { "bisque", 0xffe4c4u }, { "black", 0x000000u },
    { "blanchedalmond", 0xffebcdu }, { "blue", 0x0000ffu },
    { "blueviolet", 0x8a2be2u }, { "brown", 0xa52a2au },
    { "burlywood", 0xdeb887u }, { "cadetblue", 0x5f9ea0u },
    { "chartreuse", 0x7fff00u }, { "chocolate", 0xd2691eu },
    { "coral", 0xff7f50u }, { "cornflowerblue", 0x6495edu },
    { "cornsilk", 0xfff8dcu }, { "crimson", 0xdc143cu },
    { "cyan", 0x00ffffu }, { "darkblue", 0x00008bu },
    { "darkcyan", 0x008b8bu }, { "darkgoldenrod", 0xb8860bu },
    { "darkgray", 0xa9a9a9u }, { "darkgreen", 0x006400u },
    { "darkgrey", 0xa9a9a9u }, { "darkkhaki", 0xbdb76bu },
    { "darkmagenta", 0x8b008bu }, { "darkolivegreen", 0x556b2fu },
    { "darkorange", 0xff8c00u }, { "darkorchid", 0x9932ccu },
    { "darkred", 0x8b0000u }, { "darksalmon", 0xe9967au },
    { "darkseagreen", 0x8fbc8fu }, { "darkslateblue", 0x483d8bu },
    { "darkslategray", 0x2f4f4fu }, { "darkslategrey", 0x2f4f4fu },
    { "darkturquoise", 0x00ced1u }, { "darkviolet", 0x9400d3u },
    { "deeppink", 0xff1493u }, { "deepskyblue", 0x00bfffu },
    { "dimgray", 0x696969u }, { "dimgrey", 0x696969u },
    { "dodgerblue", 0x1e90ffu }, { "firebrick", 0xb22222u },
    { "floralwhite", 0xfffaf0u }, { "forestgreen", 0x228b22u },
    { "fuchsia", 0xff00ffu }, { "gainsboro", 0xdcdcdcu },
    { "ghostwhite", 0xf8f8ffu }, { "gold", 0xffd700u },
    { "goldenrod", 0xdaa520u }, { "gray", 0x808080u },
    { "green", 0x008000u }, { "greenyellow", 0xadff2fu },
    { "grey", 0x808080u }, { "honeydew", 0xf0fff0u },
    { "hotpink", 0xff69b4u }, { "indianred", 0xcd5c5cu },
    { "indigo", 0x4b0082u }, { "ivory", 0xfffff0u },
    { "khaki", 0xf0e68cu }, { "lavender", 0xe6e6fau },
    { "lavenderblush", 0xfff0f5u }, { "lawngreen", 0x7cfc00u },
    { "lemonchiffon", 0xfffacdu }, { "lightblue", 0xadd8e6u },
    { "lightcoral", 0xf08080u }, { "lightcyan", 0xe0ffffu },
    { "lightgoldenrodyellow", 0xfafad2u }, { "lightgray", 0xd3d3d3u },
    { "lightgreen", 0x90ee90u }, { "lightgrey", 0xd3d3d3u },
    { "lightpink", 0xffb6c1u }, { "lightsalmon", 0xffa07au },
    { "lightseagreen", 0x20b2aau }, { "lightskyblue", 0x87cefau },
    { "lightslategray", 0x778899u }, { "lightslategrey", 0x778899u },
    { "lightsteelblue", 0xb0c4deu }, { "lightyellow", 0xffffe0u },
    { "lime", 0x00ff00u }, { "limegreen", 0x32cd32u },
    { "linen", 0xfaf0e6u }, { "magenta", 0xff00ffu },
    { "maroon", 0x800000u }, { "mediumaquamarine", 0x66cdaau },
    { "mediumblue", 0x0000cdu }, { "mediumorchid", 0xba55d3u },
    { "mediumpurple", 0x9370dbu }, { "mediumseagreen", 0x3cb371u },
    { "mediumslateblue", 0x7b68eeu }, { "mediumspringgreen", 0x00fa9au },
    { "mediumturquoise", 0x48d1ccu }, { "mediumvioletred", 0xc71585u },
    { "midnightblue", 0x191970u }, { "mintcream", 0xf5fffau },
    { "mistyrose", 0xffe4e1u }, { "moccasin", 0xffe4b5u },
    { "navajowhite", 0xffdeadu }, { "navy", 0x000080u },
    { "oldlace", 0xfdf5e6u }, { "olive", 0x808000u },
    { "olivedrab", 0x6b8e23u }, { "orange", 0xffa500u },
    { "orangered", 0xff4500u }, { "orchid", 0xda70d6u },
    { "palegoldenrod", 0xeee8aau }, { "palegreen", 0x98fb98u },
    { "paleturquoise", 0xafeeeeu }, { "palevioletred", 0xdb7093u },
    { "papayawhip", 0xffefd5u }, { "peachpuff", 0xffdab9u },
    { "peru", 0xcd853fu }, { "pink", 0xffc0cbu },
    { "plum", 0xdda0ddu }, { "powderblue", 0xb0e0e6u },
    { "purple", 0x800080u }, { "rebeccapurple", 0x663399u },
    { "red", 0xff0000u }, { "rosybrown", 0xbc8f8fu },
    { "royalblue", 0x4169e1u }, { "saddlebrown", 0x8b4513u },
    { "salmon", 0xfa8072u }, { "sandybrown", 0xf4a460u },
    { "seagreen", 0x2e8b57u }, { "seashell", 0xfff5eeu },
    { "sienna", 0xa0522du }, { "silver", 0xc0c0c0u },
    { "skyblue", 0x87ceebu }, { "slateblue", 0x6a5acdu },
    { "slategray", 0x708090u }, { "slategrey", 0x708090u },
    { "snow", 0xfffafau }, { "springgreen", 0x00ff7fu },
    { "steelblue", 0x4682b4u }, { "tan", 0xd2b48cu },
    { "teal", 0x008080u }, { "thistle", 0xd8bfd8u },
    { "tomato", 0xff6347u }, { "turquoise", 0x40e0d0u },
    { "violet", 0xee82eeu }, { "wheat", 0xf5deb3u },
    { "white", 0xffffffu }, { "whitesmoke", 0xf5f5f5u },
    { "yellow", 0xffff00u }, { "yellowgreen", 0x9acd32u }
};

int robo_css_color_from_name(robo_css_slice_t name, robo_css_color_t *out)
{
    size_t index;

    if (!out || robo_css_slice_is_absent(name) || name.length == 0u) {
        return 0;
    }
    for (index = 0u;
         index < sizeof(computed_named_colors) / sizeof(computed_named_colors[0]);
         index++) {
        if (!robo_css_bytes_equal_ascii_ci(name.bytes, name.length,
                                           computed_named_colors[index].name)) {
            continue;
        }
        *out = robo_css_color_make((uint8_t)((computed_named_colors[index].rgb >> 16) & 0xffu),
                                   (uint8_t)((computed_named_colors[index].rgb >> 8) & 0xffu),
                                   (uint8_t)(computed_named_colors[index].rgb & 0xffu),
                                   255u);
        return 1;
    }
    return 0;
}

/** The value a hex digit spells, or -1 when the byte is not one. */
static int computed_hex_digit(uint8_t byte)
{
    if (byte >= (uint8_t)'0' && byte <= (uint8_t)'9') {
        return (int)(byte - (uint8_t)'0');
    }
    if (byte >= (uint8_t)'a' && byte <= (uint8_t)'f') {
        return (int)(byte - (uint8_t)'a') + 10;
    }
    if (byte >= (uint8_t)'A' && byte <= (uint8_t)'F') {
        return (int)(byte - (uint8_t)'A') + 10;
    }
    return -1;
}

int robo_css_color_from_hex(robo_css_slice_t digits, robo_css_color_t *out)
{
    int high[8];
    int low[8];
    size_t index;

    if (!out || robo_css_slice_is_absent(digits)) {
        return 0;
    }
    if (digits.length != 3u && digits.length != 4u && digits.length != 6u &&
        digits.length != 8u) {
        return 0;
    }
    for (index = 0u; index < digits.length; index++) {
        int value = computed_hex_digit(digits.bytes[index]);

        if (value < 0) {
            return 0;
        }
        high[index] = value;
        low[index] = value;
    }

    switch (digits.length) {
    case 3u:
        /* `#rgb` is each nibble doubled, which is not the same as a zero-padded
         * byte: `#f00` is (0xff, 0x00, 0x00), and reading it as (0x0f, 0x00,
         * 0x00) is the classic three-digit mistake. */
        *out = robo_css_color_make((uint8_t)(high[0] * 17), (uint8_t)(high[1] * 17),
                                   (uint8_t)(high[2] * 17), 255u);
        return 1;
    case 4u:
        *out = robo_css_color_make((uint8_t)(high[0] * 17), (uint8_t)(high[1] * 17),
                                   (uint8_t)(high[2] * 17), (uint8_t)(high[3] * 17));
        return 1;
    case 6u:
        *out = robo_css_color_make((uint8_t)((high[0] << 4) | low[1]),
                                   (uint8_t)((high[2] << 4) | low[3]),
                                   (uint8_t)((high[4] << 4) | low[5]), 255u);
        return 1;
    default:
        *out = robo_css_color_make((uint8_t)((high[0] << 4) | low[1]),
                                   (uint8_t)((high[2] << 4) | low[3]),
                                   (uint8_t)((high[4] << 4) | low[5]),
                                   (uint8_t)((high[6] << 4) | low[7]));
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* rgb()/rgba()                                                       */
/* ------------------------------------------------------------------ */

/*
 * Two helpers from the component-value section below, declared here because
 * this section is the first to need them: "is this child trivia" and "is this
 * function named X". They are defined there because that is where reading a
 * component value lives, and moving them up would split that story in two.
 */
static int computed_is_trivia(const robo_css_value_t *value);
static int computed_function_named(const robo_css_value_t *value, const char *name);

/** One rgb() argument as it was written: a number or a percentage. */
typedef struct computed_rgb_argument {
    double value;
    int is_percentage;
} computed_rgb_argument_t;

/** Which of the two argument-list syntaxes a call used. */
typedef enum computed_rgb_style {
    COMPUTED_RGB_STYLE_NONE = 0,
    COMPUTED_RGB_STYLE_COMMA,
    COMPUTED_RGB_STYLE_SPACE
} computed_rgb_style_t;

/**
 * Split an rgb() call's children into arguments and the separator style.
 *
 * The children arrive with their whitespace tokens intact (the grammar trims a
 * value's ends, not its interior), so this walks the significant components and
 * reads what stands between the arguments. CSS has two argument-list syntaxes
 * and they do not mix, so a call that uses commas between one pair and nothing
 * but space between the next is malformed rather than "close enough": the
 * separators are tracked as a *style*, and a second style is refused.
 *
 * Whitespace touching a comma is not a separator, which is what makes
 * `rgb(1 , 2 , 3)` legal: the comma is the separator and the space around it is
 * decoration. That is why the comma clears whatever whitespace preceded it
 * instead of the whitespace being remembered across it.
 *
 * `/` is the modern syntax's alpha separator and is only reachable from the
 * space style, so `rgb(1, 2, 3 / 4)` never gets past its slash.
 *
 * @return non-zero when the list is well formed. The argument count and whether
 *         a slash was seen are what tell the caller which shape it is; the count
 *         and the style are not checked against what the *function name* means
 *         here, because both spellings accept both shapes.
 */
static int computed_rgb_split(const robo_css_value_t *function,
                              computed_rgb_argument_t *arguments,
                              size_t argument_capacity,
                              size_t *out_count,
                              int *out_saw_comma,
                              int *out_saw_slash)
{
    computed_rgb_style_t style = COMPUTED_RGB_STYLE_NONE;
    size_t count = 0u;
    int saw_slash = 0;
    int pending_comma = 0;
    int pending_space = 0;
    int after_slash = 0;
    int last_was_argument = 0;
    size_t index;

    *out_count = 0u;
    *out_saw_comma = 0;
    *out_saw_slash = 0;
    for (index = 0u; index < function->children.count; index++) {
        const robo_css_value_t *child = &function->children.items[index];

        if (computed_is_trivia(child)) {
            /* Only whitespace *between* arguments separates them; a comment is
             * trivia too and is treated the same way. */
            if (last_was_argument) {
                pending_space = 1;
            }
            continue;
        }
        if (child->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
            return 0;
        }
        switch (child->token.type) {
        case ROBO_CSS_TOKEN_COMMA:
            /* A comma with no argument before it, or two in a row, is not a
             * separator. */
            if (!last_was_argument) {
                return 0;
            }
            pending_comma = 1;
            pending_space = 0;
            last_was_argument = 0;
            continue;
        case ROBO_CSS_TOKEN_DELIM:
            if (!robo_css_token_equals_ascii(&child->token, "/")) {
                return 0;
            }
            if (!last_was_argument || saw_slash || style == COMPUTED_RGB_STYLE_COMMA) {
                return 0;
            }
            style = COMPUTED_RGB_STYLE_SPACE;
            saw_slash = 1;
            after_slash = 1;
            pending_comma = 0;
            pending_space = 0;
            last_was_argument = 0;
            continue;
        case ROBO_CSS_TOKEN_NUMBER:
        case ROBO_CSS_TOKEN_PERCENTAGE:
            break;
        default:
            return 0;
        }
        if (count > 0u && !after_slash) {
            /* Every argument after the first needs a separator, and it has to
             * be the same one the call started with. */
            computed_rgb_style_t separator = pending_comma ? COMPUTED_RGB_STYLE_COMMA
                                                           : COMPUTED_RGB_STYLE_SPACE;

            if (!pending_comma && !pending_space) {
                return 0;
            }
            if (style == COMPUTED_RGB_STYLE_NONE) {
                style = separator;
            } else if (style != separator) {
                return 0;
            }
        }
        after_slash = 0;
        if (count >= argument_capacity) {
            return 0;
        }
        if (!computed_number_is_finite(child->token.number)) {
            return 0;
        }
        arguments[count].value = child->token.number;
        arguments[count].is_percentage =
            child->token.type == ROBO_CSS_TOKEN_PERCENTAGE;
        count++;
        last_was_argument = 1;
        pending_comma = 0;
        pending_space = 0;
    }
    /* A trailing separator is a missing argument. */
    if (!last_was_argument) {
        return 0;
    }
    *out_count = count;
    *out_saw_comma = style == COMPUTED_RGB_STYLE_COMMA;
    *out_saw_slash = saw_slash;
    return 1;
}

/** One channel value in 0–255, from a number or a percentage. */
static uint8_t computed_rgb_channel(computed_rgb_argument_t argument)
{
    return argument.is_percentage ? computed_channel_percent(argument.value)
                                  : computed_channel_255(argument.value);
}

/** One alpha value in 0–255, from a number or a percentage. */
static uint8_t computed_rgb_alpha(computed_rgb_argument_t argument)
{
    return argument.is_percentage ? computed_alpha_percent(argument.value)
                                  : computed_alpha_number(argument.value);
}

int robo_css_color_from_rgb_function(const robo_css_value_t *function,
                                     robo_css_color_t *out)
{
    computed_rgb_argument_t arguments[4];
    size_t count = 0u;
    int saw_comma = 0;
    int saw_slash = 0;

    if (!function || !out || function->kind != ROBO_CSS_VALUE_KIND_FUNCTION) {
        return 0;
    }
    if (!computed_function_named(function, "rgb") &&
        !computed_function_named(function, "rgba")) {
        return 0;
    }
    if (!computed_rgb_split(function, arguments, 4u, &count, &saw_comma, &saw_slash)) {
        return 0;
    }
    if (count != 3u && count != 4u) {
        return 0;
    }
    /* A fourth argument is always the alpha, and it is only written one of two
     * ways: after the legacy comma, or after the modern slash. Four numbers
     * separated by nothing but spaces — `rgb(1 2 3 4)` — has neither, and is a
     * malformed call rather than an opaque one. */
    if (count == 4u && !saw_slash && !saw_comma) {
        return 0;
    }
    if (saw_slash && count != 4u) {
        return 0;
    }
    /* A four-argument comma call is the legacy `rgba()`, and a three-argument
     * one is fully opaque. Both spellings of the function name accept both
     * shapes in CSS Color 4, so nothing here distinguishes `rgb` from `rgba`. */
    *out = robo_css_color_make(computed_rgb_channel(arguments[0]),
                               computed_rgb_channel(arguments[1]),
                               computed_rgb_channel(arguments[2]),
                               count == 4u ? computed_rgb_alpha(arguments[3]) : 255u);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Reading a component value                                          */
/* ------------------------------------------------------------------ */

/** Non-zero when @p value is trivia: whitespace or a comment. */
static int computed_is_trivia(const robo_css_value_t *value)
{
    return value->kind == ROBO_CSS_VALUE_KIND_TOKEN &&
           (value->token.type == ROBO_CSS_TOKEN_WHITESPACE ||
            value->token.type == ROBO_CSS_TOKEN_COMMENT);
}

/**
 * The one significant component of @p value, or NULL when it is not "one".
 *
 * A declaration's value arrives as a LIST (the grammar builds every one that
 * way) whose ends are already trimmed, so `red` is a list of one ident. A
 * hand-built value may be a bare token, which is accepted too. Anything else —
 * nothing, or two or more significant components — answers NULL, and every
 * normaliser that needs a single component turns that into INVALID rather than
 * picking one of them.
 */
static const robo_css_value_t *computed_single(const robo_css_value_t *value)
{
    const robo_css_value_t *found = NULL;
    size_t index;

    if (!value || value->kind == ROBO_CSS_VALUE_KIND_NONE) {
        return NULL;
    }
    if (value->kind != ROBO_CSS_VALUE_KIND_LIST) {
        return value;
    }
    for (index = 0u; index < value->children.count; index++) {
        const robo_css_value_t *child = &value->children.items[index];

        if (computed_is_trivia(child)) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = child;
    }
    return found;
}

/** The token of a single non-trivia component, or NULL. */
static const robo_css_token_t *computed_single_token(const robo_css_value_t *value)
{
    const robo_css_value_t *single = computed_single(value);

    if (!single || single->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        return NULL;
    }
    if (single->token.type == ROBO_CSS_TOKEN_WHITESPACE ||
        single->token.type == ROBO_CSS_TOKEN_COMMENT) {
        return NULL;
    }
    return &single->token;
}

/**
 * Non-zero when @p value is a function whose name is @p name, considering only
 * the functions this build understands by name.
 */
static int computed_function_named(const robo_css_value_t *value, const char *name)
{
    if (!value || value->kind != ROBO_CSS_VALUE_KIND_FUNCTION) {
        return 0;
    }
    return robo_css_bytes_equal_ascii_ci(value->name.bytes, value->name.length, name);
}

/**
 * Non-zero when @p value is a function this build recognises but cannot compute
 * — `calc()` and the maths family, `var()` and the environment family.
 *
 * They are *recognised* rather than invalid, and that is the point: `calc(100%
 * - 20px)` is a perfectly good computed value that only a layout pass can
 * reduce, and calling it invalid would delete a declaration the author wrote.
 * Carried PRESERVED with the unresolved flag set, it survives to the phase that
 * has the context, which is the correct behaviour for every one of these.
 */
static int computed_function_is_unresolved(const robo_css_value_t *value)
{
    static const char *const names[] = { "calc", "min", "max", "clamp", "var", "env" };
    size_t index;

    if (!value || value->kind != ROBO_CSS_VALUE_KIND_FUNCTION) {
        return 0;
    }
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        if (robo_css_bytes_equal_ascii_ci(value->name.bytes, value->name.length,
                                          names[index])) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

void robo_css_computed_value_init(robo_css_computed_value_t *value)
{
    if (!value) {
        return;
    }
    memset(value, 0, sizeof(*value));
    value->kind = ROBO_CSS_COMPUTED_VALUE_NONE;
    value->unit = ROBO_CSS_UNIT_NONE;
    robo_css_value_init(&value->preserved);
}

void robo_css_computed_value_dispose(robo_css_computed_value_t *value)
{
    if (!value) {
        return;
    }
    robo_free(value->text);
    robo_css_value_dispose(&value->preserved);
    robo_css_computed_value_init(value);
}

robo_result_t robo_css_computed_value_move(robo_css_computed_value_t *value,
                                          robo_css_computed_value_t *out_value,
                                          robo_css_diag_t *diag)
{
    if (!value || !out_value || value == out_value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a computed value cannot move into itself");
    }
    robo_css_computed_value_dispose(out_value);
    *out_value = *value;
    robo_css_computed_value_init(value);
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_copy(robo_css_computed_value_t *out_value,
                                          const robo_css_computed_value_t *value,
                                          robo_css_diag_t *diag)
{
    robo_result_t result;

    if (!out_value || !value || out_value == value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a computed value cannot copy into itself");
    }
    robo_css_computed_value_dispose(out_value);
    *out_value = *value;
    out_value->text = NULL;
    out_value->text_length = 0u;
    robo_css_value_init(&out_value->preserved);

    if (value->text) {
        out_value->text = (char *)robo_malloc(value->text_length + 1u);
        if (!out_value->text) {
            robo_css_computed_value_init(out_value);
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                                 "could not copy the computed value's text");
        }
        memcpy(out_value->text, value->text, value->text_length);
        out_value->text[value->text_length] = '\0';
        /* The length goes back only once the bytes it describes exist: a value
         * whose text pointer is set but whose length is still zero reads as
         * empty, which is how a copied keyword (`red`) would arrive at a child
         * as no keyword at all. */
        out_value->text_length = value->text_length;
    }
    if (value->preserved.kind != ROBO_CSS_VALUE_KIND_NONE) {
        result = robo_css_value_copy(&out_value->preserved, &value->preserved, diag);
        if (result != ROBO_OK) {
            robo_free(out_value->text);
            robo_css_computed_value_init(out_value);
            return result;
        }
    }
    return ROBO_OK;
}

int robo_css_computed_value_is_empty(const robo_css_computed_value_t *value)
{
    return !value || value->kind == ROBO_CSS_COMPUTED_VALUE_NONE;
}

/* ------------------------------------------------------------------ */
/* Kinds                                                              */
/* ------------------------------------------------------------------ */

int robo_css_computed_value_kind_is_known(robo_css_computed_value_kind_t kind)
{
    return kind >= ROBO_CSS_COMPUTED_VALUE_NONE &&
           kind < (robo_css_computed_value_kind_t)ROBO_CSS_COMPUTED_VALUE_KIND_COUNT;
}

const char *robo_css_computed_value_kind_name(robo_css_computed_value_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_COMPUTED_VALUE_NONE:
        return "none";
    case ROBO_CSS_COMPUTED_VALUE_KEYWORD:
        return "keyword";
    case ROBO_CSS_COMPUTED_VALUE_NUMBER:
        return "number";
    case ROBO_CSS_COMPUTED_VALUE_PERCENTAGE:
        return "percentage";
    case ROBO_CSS_COMPUTED_VALUE_LENGTH:
        return "length";
    case ROBO_CSS_COMPUTED_VALUE_COLOR:
        return "colour";
    case ROBO_CSS_COMPUTED_VALUE_FAMILY_LIST:
        return "family-list";
    case ROBO_CSS_COMPUTED_VALUE_PRESERVED:
        return "preserved";
    case ROBO_CSS_COMPUTED_VALUE_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Construction                                                       */
/* ------------------------------------------------------------------ */

/** Own a copy of @p bytes in @p value->text, or fail without changing it. */
static int computed_adopt_text(robo_css_computed_value_t *value,
                               const uint8_t *bytes,
                               size_t length)
{
    char *copy = (char *)robo_malloc(length + 1u);

    if (!copy) {
        return 0;
    }
    if (length > 0u) {
        memcpy(copy, bytes, length);
    }
    copy[length] = '\0';
    robo_free(value->text);
    value->text = copy;
    value->text_length = length;
    return 1;
}

robo_result_t robo_css_computed_value_make_keyword(robo_css_computed_value_t *value,
                                                  robo_css_slice_t keyword,
                                                  robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    if (!computed_adopt_text(value, keyword.bytes, keyword.length)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                             "could not copy the keyword");
    }
    value->kind = ROBO_CSS_COMPUTED_VALUE_KEYWORD;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_number(robo_css_computed_value_t *value,
                                                 double number,
                                                 int is_integer,
                                                 robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    value->kind = ROBO_CSS_COMPUTED_VALUE_NUMBER;
    value->number = number;
    value->is_integer = is_integer ? 1 : 0;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_percentage(robo_css_computed_value_t *value,
                                                     double percentage,
                                                     int is_integer,
                                                     robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    value->kind = ROBO_CSS_COMPUTED_VALUE_PERCENTAGE;
    value->number = percentage;
    value->is_integer = is_integer ? 1 : 0;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_length(robo_css_computed_value_t *value,
                                                 double number,
                                                 robo_css_unit_t unit,
                                                 int is_integer,
                                                 robo_css_diag_t *diag)
{
    if (!value || !robo_css_unit_is_known(unit) || unit == ROBO_CSS_UNIT_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a length needs a known, non-empty unit");
    }
    robo_css_computed_value_dispose(value);
    value->kind = ROBO_CSS_COMPUTED_VALUE_LENGTH;
    value->number = number;
    value->unit = unit;
    value->is_integer = is_integer ? 1 : 0;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_color(robo_css_computed_value_t *value,
                                                robo_css_color_t color,
                                                robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    value->kind = ROBO_CSS_COMPUTED_VALUE_COLOR;
    value->color = color;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_family_list(robo_css_computed_value_t *value,
                                                       robo_css_slice_t families,
                                                       robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    if (!computed_adopt_text(value, families.bytes, families.length)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                             "could not copy the family list");
    }
    value->kind = ROBO_CSS_COMPUTED_VALUE_FAMILY_LIST;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_preserved(robo_css_computed_value_t *value,
                                                     const robo_css_value_t *specified,
                                                     robo_css_diag_t *diag)
{
    if (!value || !specified) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value or specified value is NULL");
    }
    robo_css_computed_value_dispose(value);
    if (specified->kind == ROBO_CSS_VALUE_KIND_NONE) {
        /* Nothing to preserve. NONE is the honest answer: "there was no value"
         * is not "here is a value that means nothing". */
        return ROBO_OK;
    }
    value->kind = ROBO_CSS_COMPUTED_VALUE_PRESERVED;
    {
        robo_result_t result = robo_css_value_copy(&value->preserved, specified, diag);

        if (result != ROBO_OK) {
            robo_css_computed_value_init(value);
            return result;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_invalid(robo_css_computed_value_t *value,
                                                   const char *reason,
                                                   robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    if (!computed_adopt_text(value, (const uint8_t *)(reason ? reason : ""),
                             reason ? strlen(reason) : 0u)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                             "could not copy the reason");
    }
    value->kind = ROBO_CSS_COMPUTED_VALUE_INVALID;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Reading                                                            */
/* ------------------------------------------------------------------ */

robo_css_slice_t robo_css_computed_value_text(const robo_css_computed_value_t *value)
{
    if (!value || !value->text) {
        return robo_css_slice_empty();
    }
    return robo_css_slice_from_bytes((const uint8_t *)value->text, value->text_length);
}

const robo_css_value_t *robo_css_computed_value_preserved(
    const robo_css_computed_value_t *value)
{
    if (!value || value->kind != ROBO_CSS_COMPUTED_VALUE_PRESERVED) {
        return NULL;
    }
    return &value->preserved;
}

robo_css_color_t robo_css_computed_value_color(const robo_css_computed_value_t *value)
{
    if (!value || value->kind != ROBO_CSS_COMPUTED_VALUE_COLOR) {
        return robo_css_color_make(0u, 0u, 0u, 0u);
    }
    return value->color;
}

/* ------------------------------------------------------------------ */
/* Equality                                                           */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when two tokens' *values* are the same bytes.
 *
 * The raw text is the fast path and the correct one whenever neither token
 * contains an escape, which is the overwhelming majority: an escape always
 * changes the value's length, so equal text length and value length proves
 * there is none. When there is one, both values are decoded and compared,
 * because `\72 ed` and `red` are the same token and a comparison that said
 * otherwise would make two identical styles unequal.
 */
static int computed_token_value_equals(const robo_css_token_t *a,
                                       const robo_css_token_t *b)
{
    uint8_t scratch_a[128];
    uint8_t scratch_b[128];
    uint8_t *bytes_a = scratch_a;
    uint8_t *bytes_b = scratch_b;
    size_t length_a = robo_css_token_value_length(a);
    size_t length_b = robo_css_token_value_length(b);
    int equal;

    if (length_a != length_b) {
        return 0;
    }
    if (a->text.length == length_a && b->text.length == length_b) {
        return robo_css_slice_equals(a->text, b->text);
    }
    if (length_a > sizeof(scratch_a)) {
        bytes_a = (uint8_t *)robo_malloc(length_a);
        bytes_b = (uint8_t *)robo_malloc(length_b);
        if (!bytes_a || !bytes_b) {
            robo_free(bytes_a == scratch_a ? NULL : bytes_a);
            robo_free(bytes_b == scratch_b ? NULL : bytes_b);
            return 0;
        }
    }
    equal = robo_css_token_value_copy(a, bytes_a, length_a, &length_a, NULL) == ROBO_OK &&
            robo_css_token_value_copy(b, bytes_b, length_b, &length_b, NULL) == ROBO_OK &&
            length_a == length_b &&
            (length_a == 0u || memcmp(bytes_a, bytes_b, length_a) == 0);
    if (bytes_a != scratch_a) {
        robo_free(bytes_a);
        robo_free(bytes_b);
    }
    return equal;
}

/** Structural equality of two component-value trees. */
static int computed_trees_equal(const robo_css_value_t *a, const robo_css_value_t *b)
{
    size_t index;

    if (a->kind != b->kind) {
        return 0;
    }
    switch (a->kind) {
    case ROBO_CSS_VALUE_KIND_NONE:
        return 1;
    case ROBO_CSS_VALUE_KIND_TOKEN:
        /* The resolved value, not the raw text: `\72 ed` and `red` are the same
         * token, and two values that mean the same thing must compare equal or
         * a test cannot say what it expects. */
        return a->token.type == b->token.type &&
               computed_token_value_equals(&a->token, &b->token);
    case ROBO_CSS_VALUE_KIND_FUNCTION:
        if (a->children.count != b->children.count ||
            !robo_css_slice_equals_ci(a->name, b->name)) {
            return 0;
        }
        break;
    case ROBO_CSS_VALUE_KIND_BLOCK:
        if (a->children.count != b->children.count || a->open != b->open ||
            a->close != b->close) {
            return 0;
        }
        break;
    default:
        if (a->children.count != b->children.count) {
            return 0;
        }
        break;
    }
    for (index = 0u; index < a->children.count; index++) {
        if (!computed_trees_equal(&a->children.items[index], &b->children.items[index])) {
            return 0;
        }
    }
    return 1;
}

int robo_css_computed_value_equals(const robo_css_computed_value_t *a,
                                   const robo_css_computed_value_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind || a->unresolved != b->unresolved) {
        return 0;
    }
    switch (a->kind) {
    case ROBO_CSS_COMPUTED_VALUE_NONE:
        return 1;
    case ROBO_CSS_COMPUTED_VALUE_KEYWORD:
        return robo_css_slice_equals(robo_css_computed_value_text(a),
                                     robo_css_computed_value_text(b));
    case ROBO_CSS_COMPUTED_VALUE_FAMILY_LIST:
    case ROBO_CSS_COMPUTED_VALUE_INVALID:
        return robo_css_slice_equals(robo_css_computed_value_text(a),
                                     robo_css_computed_value_text(b));
    case ROBO_CSS_COMPUTED_VALUE_NUMBER:
        return a->is_integer == b->is_integer && a->number == b->number;
    case ROBO_CSS_COMPUTED_VALUE_PERCENTAGE:
        return a->is_integer == b->is_integer && a->number == b->number;
    case ROBO_CSS_COMPUTED_VALUE_LENGTH:
        return a->unit == b->unit && a->is_integer == b->is_integer &&
               a->number == b->number;
    case ROBO_CSS_COMPUTED_VALUE_COLOR:
        return robo_css_color_equals(a->color, b->color);
    default:
        return computed_trees_equal(&a->preserved, &b->preserved);
    }
}

/* ------------------------------------------------------------------ */
/* Rendering                                                          */
/* ------------------------------------------------------------------ */

/** The character a block's opening token type stands for. */
static char computed_bracket_char(robo_css_token_type_t type)
{
    switch (type) {
    case ROBO_CSS_TOKEN_LEFT_SQUARE:
        return '[';
    case ROBO_CSS_TOKEN_LEFT_CURLY:
        return '{';
    default:
        return '(';
    }
}

static char computed_bracket_close(robo_css_token_type_t type)
{
    switch (type) {
    case ROBO_CSS_TOKEN_LEFT_SQUARE:
        return ']';
    case ROBO_CSS_TOKEN_LEFT_CURLY:
        return '}';
    default:
        return ')';
    }
}

/**
 * Write @p value's *number* into the buffer, trimming meaningless zeroes.
 *
 * Integers render without a decimal point because a value written as `10` is a
 * different value from `10.0` in CSS only where the property says so, and the
 * `is_integer` flag already records that. A non-integer renders with at most six
 * decimals — enough that every length and colour channel this build produces
 * round-trips, and few enough that `0.1 + 0.2` does not print seventeen digits.
 */
static int computed_write_number(double value, int is_integer,
                                 char *buffer, size_t capacity, size_t *used)
{
    char digits[64];
    int written;
    size_t length;

    if (capacity <= *used + 1u) {
        return 0;
    }
    written = snprintf(digits, sizeof(digits),
                       is_integer ? "%.0f" : "%.6f", value);
    if (written < 0 || (size_t)written >= sizeof(digits)) {
        return 0;
    }
    length = (size_t)written;
    if (!is_integer) {
        while (length > 0u && digits[length - 1u] == '0') {
            length--;
        }
        if (length > 0u && digits[length - 1u] == '.') {
            length--;
        }
        if (length == 0u) {
            /* `-0.000000` is zero, and zero renders as `0`. */
            digits[0] = '0';
            length = 1u;
        }
    }
    if (capacity <= *used + length) {
        return 0;
    }
    memcpy(buffer + *used, digits, length);
    *used += length;
    buffer[*used] = '\0';
    return 1;
}

static int computed_write_into(const robo_css_value_t *value, char *buffer,
                               size_t capacity, size_t *used);

static int computed_write_children(const robo_css_value_t *value, char *buffer,
                                   size_t capacity, size_t *used)
{
    size_t index;

    for (index = 0u; index < value->children.count; index++) {
        if (!computed_write_into(&value->children.items[index], buffer, capacity,
                                 used)) {
            return 0;
        }
    }
    return 1;
}

static int computed_append_char(char *buffer, size_t capacity, size_t *used, char byte)
{
    if (capacity <= *used + 1u) {
        return 0;
    }
    buffer[*used] = byte;
    (*used)++;
    buffer[*used] = '\0';
    return 1;
}

static int computed_write_token_text(const robo_css_value_t *value, char *buffer,
                                     size_t capacity, size_t *used);

static int computed_write_into(const robo_css_value_t *value, char *buffer,
                               size_t capacity, size_t *used)
{
    switch (value->kind) {
    case ROBO_CSS_VALUE_KIND_TOKEN:
        /* The raw text: this rendering is a diagnostic, and for a preserved
         * tree the source spelling is what a developer wants to see. */
        return computed_write_token_text(value, buffer, capacity, used);
    case ROBO_CSS_VALUE_KIND_FUNCTION:
        if (value->name.length > capacity - *used - 1u) {
            return 0;
        }
        if (value->name.length > 0u) {
            memcpy(buffer + *used, value->name.bytes, value->name.length);
            *used += value->name.length;
            buffer[*used] = '\0';
        }
        return computed_append_char(buffer, capacity, used, '(') &&
               computed_write_children(value, buffer, capacity, used) &&
               computed_append_char(buffer, capacity, used, ')');
    case ROBO_CSS_VALUE_KIND_BLOCK:
        return computed_append_char(buffer, capacity, used,
                                    computed_bracket_char(value->open)) &&
               computed_write_children(value, buffer, capacity, used) &&
               computed_append_char(buffer, capacity, used,
                                    computed_bracket_close(value->open));
    case ROBO_CSS_VALUE_KIND_LIST:
        return computed_write_children(value, buffer, capacity, used);
    default:
        return 1;
    }
}

/**
 * One token's raw text, appended to the buffer.
 *
 * The raw text rather than the decoded value, because this rendering exists so
 * that a developer can see the tokens a PRESERVED value carries and the source
 * spelling is exactly what they wrote.
 */
static int computed_write_token_text(const robo_css_value_t *value, char *buffer,
                                     size_t capacity, size_t *used)
{
    if (value->token.text.length > capacity - *used - 1u) {
        return 0;
    }
    if (value->token.text.length > 0u) {
        memcpy(buffer + *used, value->token.text.bytes, value->token.text.length);
        *used += value->token.text.length;
        buffer[*used] = '\0';
    }
    return 1;
}

robo_result_t robo_css_computed_value_write(const robo_css_computed_value_t *value,
                                            char *buffer,
                                            size_t capacity,
                                            size_t *out_length,
                                            robo_css_diag_t *diag)
{
    size_t used = 0u;
    int ok = 1;

    if (!value || !buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value, buffer and out_length are required");
    }
    buffer[0] = '\0';

    switch (value->kind) {
    case ROBO_CSS_COMPUTED_VALUE_NONE:
        break;
    case ROBO_CSS_COMPUTED_VALUE_KEYWORD:
    case ROBO_CSS_COMPUTED_VALUE_FAMILY_LIST:
    case ROBO_CSS_COMPUTED_VALUE_INVALID:
        if (value->text_length >= capacity) {
            *out_length = value->text_length;
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED,
                                 "the computed value's text does not fit");
        }
        if (value->text_length > 0u) {
            memcpy(buffer, value->text, value->text_length);
        }
        buffer[value->text_length] = '\0';
        used = value->text_length;
        break;
    case ROBO_CSS_COMPUTED_VALUE_NUMBER:
        ok = computed_write_number(value->number, value->is_integer, buffer, capacity,
                                   &used);
        break;
    case ROBO_CSS_COMPUTED_VALUE_PERCENTAGE:
        ok = computed_write_number(value->number, value->is_integer, buffer, capacity,
                                   &used) &&
             computed_append_char(buffer, capacity, &used, '%');
        break;
    case ROBO_CSS_COMPUTED_VALUE_LENGTH:
        ok = computed_write_number(value->number, value->is_integer, buffer, capacity,
                                   &used);
        if (ok) {
            const char *unit = robo_css_unit_name(value->unit);
            size_t length = strlen(unit);

            if (length >= capacity - used) {
                ok = 0;
            } else {
                memcpy(buffer + used, unit, length);
                used += length;
                buffer[used] = '\0';
            }
        }
        break;
    case ROBO_CSS_COMPUTED_VALUE_COLOR:
        if (capacity <= 10u) {
            ok = 0;
            break;
        }
        used = (size_t)snprintf(buffer, capacity,
                                value->color.alpha == 255u ? "#%02x%02x%02x" : "#%02x%02x%02x%02x",
                                (unsigned)value->color.red, (unsigned)value->color.green,
                                (unsigned)value->color.blue, (unsigned)value->color.alpha);
        ok = used < capacity;
        break;
    case ROBO_CSS_COMPUTED_VALUE_PRESERVED:
        if (value->preserved.kind == ROBO_CSS_VALUE_KIND_TOKEN) {
            ok = computed_write_token_text(&value->preserved, buffer, capacity, &used);
        } else {
            ok = computed_write_into(&value->preserved, buffer, capacity, &used);
        }
        break;
    default:
        ok = 0;
        break;
    }

    if (!ok) {
        buffer[0] = '\0';
        *out_length = used;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED,
                             "the computed value's text does not fit");
    }
    *out_length = used;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Computing                                                          */
/* ------------------------------------------------------------------ */

/** Make @p out INVALID and record why, always succeeding at the report. */
static robo_result_t computed_reject(robo_css_computed_value_t *out,
                                     robo_css_property_computation_t computation,
                                     const char *reason,
                                     robo_css_diag_t *diag)
{
    char message[128];

    (void)snprintf(message, sizeof(message), "%s: %s",
                   robo_css_property_computation_name(computation), reason);
    ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED, message);
    return robo_css_computed_value_make_invalid(out, reason, NULL);
}

/** Preserve @p specified, marking it unresolved for @p out. */
static robo_result_t computed_keep(robo_css_computed_value_t *out,
                                   const robo_css_value_t *specified,
                                   int unresolved,
                                   robo_css_diag_t *diag)
{
    robo_result_t result = robo_css_computed_value_make_preserved(out, specified, diag);

    if (result == ROBO_OK) {
        out->unresolved = unresolved ? 1 : 0;
    }
    return result;
}

/**
 * A declared identifier's decoded text, lowercased.
 *
 * CSS keywords are ASCII case-insensitive and the canonical spelling is
 * lowercase, so `AUTO` and `auto` compute to the *same* computed value — which
 * is what makes `equals` usable as a test's expectation rather than as a
 * comparison of how the author typed it.
 */
static int computed_keyword_text(const robo_css_token_t *token,
                                 char *buffer, size_t capacity, size_t *out_length)
{
    size_t length = robo_css_token_value_length(token);
    size_t index;

    if (length == 0u || length >= capacity) {
        return 0;
    }
    if (robo_css_token_value_copy(token, (uint8_t *)buffer, length, out_length,
                                  NULL) != ROBO_OK) {
        return 0;
    }
    for (index = 0u; index < length; index++) {
        buffer[index] = (char)robo_css_ascii_lower((uint8_t)buffer[index]);
    }
    buffer[length] = '\0';
    *out_length = length;
    return 1;
}

/** Compute a colour: one ident, one hash, or `rgb()`/`rgba()`. */
static robo_result_t computed_compute_color(robo_css_computed_value_t *out,
                                            const robo_css_value_t *specified,
                                            robo_css_diag_t *diag)
{
    const robo_css_value_t *single = computed_single(specified);
    robo_css_color_t color;

    if (!single) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                               "not a single colour component", diag);
    }
    if (single->kind == ROBO_CSS_VALUE_KIND_FUNCTION) {
        int is_rgb = computed_function_named(single, "rgb") ||
                     computed_function_named(single, "rgba");
        int is_modern = computed_function_named(single, "hsl") ||
                        computed_function_named(single, "hsla") ||
                        computed_function_named(single, "hwb") ||
                        computed_function_named(single, "lab") ||
                        computed_function_named(single, "lch") ||
                        computed_function_named(single, "oklab") ||
                        computed_function_named(single, "oklch") ||
                        computed_function_named(single, "color") ||
                        computed_function_named(single, "color-mix");

        if (is_rgb) {
            if (robo_css_color_from_rgb_function(single, &color)) {
                return robo_css_computed_value_make_color(out, color, diag);
            }
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                   "rgb() arguments are out of range or malformed", diag);
        }
        if (is_modern) {
            /* Recognised and deliberately not computed: the colour subsystem
             * owns these, and a wrong colour is worse than a deferred one. */
            return computed_keep(out, specified, 1, diag);
        }
        if (computed_function_is_unresolved(single)) {
            return computed_keep(out, specified, 1, diag);
        }
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                               "unsupported colour function", diag);
    }
    if (single->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                               "not a colour", diag);
    }
    if (single->token.type == ROBO_CSS_TOKEN_HASH) {
        uint8_t digits[16];
        size_t length = 0u;

        if (robo_css_token_value_length(&single->token) > sizeof(digits) ||
            robo_css_token_value_copy(&single->token, digits, sizeof(digits), &length,
                                      NULL) != ROBO_OK) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                   "not a hex colour", diag);
        }
        if (robo_css_color_from_hex(robo_css_slice_from_bytes(digits, length), &color)) {
            return robo_css_computed_value_make_color(out, color, diag);
        }
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                               "not a hex colour", diag);
    }
    if (single->token.type == ROBO_CSS_TOKEN_IDENT) {
        char keyword[64];
        size_t length = 0u;
        robo_css_slice_t name;

        if (!computed_keyword_text(&single->token, keyword, sizeof(keyword), &length)) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                   "not a colour name", diag);
        }
        name = robo_css_slice_from_bytes((const uint8_t *)keyword, length);
        if (robo_css_color_from_name(name, &color)) {
            return robo_css_computed_value_make_color(out, color, diag);
        }
        if (robo_css_slice_equals_ascii(name, "transparent")) {
            return robo_css_computed_value_make_color(
                out, robo_css_color_make(0u, 0u, 0u, 0u), diag);
        }
        if (robo_css_slice_equals_ascii(name, "currentcolor")) {
            /* Recognised, carried, and *not* resolved: its value is the
             * element's own computed `color`, which is the colour subsystem's
             * job. Unresolved rather than invalid, because the author wrote
             * something legal. */
            return computed_keep(out, specified, 1, diag);
        }
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                               "unknown colour name", diag);
    }
    return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "not a colour",
                           diag);
}

/**
 * The identifiers a length-shaped property may be written with, as one set.
 *
 * The shape is shared by several properties whose keyword lists differ, so this
 * is their union: `font-size` has `medium` and the size keywords, `letter-spacing`
 * has `normal`, `width` has `auto` and the intrinsic sizes. Which of them belongs
 * to *which* property is the property registry's business; what this layer knows
 * is a value's shape.
 *
 * What it will not do is call every identifier a length keyword. `width: red`
 * is a mistake an author wants told about, and carrying it as a computed keyword
 * would hand a later phase a value it can only fail on.
 */
static const char *const computed_length_keywords[] = {
    "auto",     "none",       "normal",   "medium",   "min-content",
    "max-content", "fit-content", "smaller", "larger",   "xx-small",
    "x-small",  "small",      "large",    "x-large",  "xx-large",
    "xxx-large"
};

/** Non-zero when @p keyword (already lowercased) names a length keyword. */
static int computed_length_keyword_is_known(const char *keyword, size_t length)
{
    size_t index;

    for (index = 0u;
         index < sizeof(computed_length_keywords) / sizeof(computed_length_keywords[0]);
         index++) {
        if (robo_css_bytes_equal_ascii_ci((const uint8_t *)keyword, length,
                                          computed_length_keywords[index])) {
            return 1;
        }
    }
    return 0;
}

/** Compute a length-family value: dimension, percentage, zero, or keyword. */
static robo_result_t computed_compute_length(robo_css_computed_value_t *out,
                                             const robo_css_value_t *specified,
                                             robo_css_diag_t *diag)
{
    const robo_css_token_t *token = computed_single_token(specified);

    if (!token) {
        const robo_css_value_t *single = computed_single(specified);

        if (single && (computed_function_is_unresolved(single) ||
                       computed_function_named(single, "calc") ||
                       computed_function_named(single, "min") ||
                       computed_function_named(single, "max") ||
                       computed_function_named(single, "clamp"))) {
            return computed_keep(out, specified, 1, diag);
        }
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                               "not a single length component", diag);
    }
    if (!computed_number_is_finite(token->number)) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                               "not a finite number", diag);
    }
    switch (token->type) {
    case ROBO_CSS_TOKEN_DIMENSION: {
        robo_css_unit_t unit = robo_css_unit_from_slice(token->unit);

        if (unit == ROBO_CSS_UNIT_NONE) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                                   "unknown length unit", diag);
        }
        if (robo_css_unit_is_absolute(unit)) {
            /* `1in` and `96px` are the same length, so a computed value has one
             * of them. Converting here is exact, not an approximation. */
            return robo_css_computed_value_make_length(
                out, robo_css_unit_to_px(token->number, unit), ROBO_CSS_UNIT_PX,
                token->is_integer, diag);
        }
        return robo_css_computed_value_make_length(out, token->number, unit,
                                                   token->is_integer, diag);
    }
    case ROBO_CSS_TOKEN_PERCENTAGE:
        /* Stays a percentage: only layout knows what it is a percentage *of*. */
        return robo_css_computed_value_make_percentage(out, token->number,
                                                       token->is_integer, diag);
    case ROBO_CSS_TOKEN_NUMBER:
        if (token->number == 0.0) {
            /* CSS's one exception: a bare `0` is a length, because there is no
             * unit that changes what zero means. */
            return robo_css_computed_value_make_length(out, 0.0, ROBO_CSS_UNIT_PX, 1,
                                                       diag);
        }
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                               "a bare number is not a length", diag);
    case ROBO_CSS_TOKEN_IDENT: {
        char keyword[64];
        size_t length = 0u;

        if (!computed_keyword_text(token, keyword, sizeof(keyword), &length) ||
            !computed_length_keyword_is_known(keyword, length)) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                                   "not a length keyword", diag);
        }
        return robo_css_computed_value_make_keyword(
            out, robo_css_slice_from_bytes((const uint8_t *)keyword, length), diag);
    }
    default:
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                               "not a length", diag);
    }
}

/** Compute a single-identifier keyword. */
static robo_result_t computed_compute_keyword(robo_css_computed_value_t *out,
                                              const robo_css_value_t *specified,
                                              robo_css_diag_t *diag)
{
    const robo_css_token_t *token = computed_single_token(specified);
    const robo_css_value_t *single = computed_single(specified);
    char keyword[64];
    size_t length = 0u;

    if (!token) {
        if (single && computed_function_is_unresolved(single)) {
            return computed_keep(out, specified, 1, diag);
        }
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,
                               "not a single identifier", diag);
    }
    if (token->type != ROBO_CSS_TOKEN_IDENT) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,
                               "not an identifier", diag);
    }
    if (!computed_keyword_text(token, keyword, sizeof(keyword), &length)) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,
                               "not an identifier", diag);
    }
    return robo_css_computed_value_make_keyword(
        out, robo_css_slice_from_bytes((const uint8_t *)keyword, length), diag);
}

/**
 * Compute a font family list: identifiers and strings, comma separated.
 *
 * Two passes over the children, and the reason is the shape check. A family
 * list is a strictly alternating sequence — part, comma, part, comma, part — and
 * a validator that consumed the components while checking would have already
 * built half a string when it found the malformed part. Validating and summing
 * first, then building into exactly the right buffer, means a malformed list
 * leaves nothing behind and there is no growth logic to get wrong.
 *
 * `font-family: ""` is refused: an empty family name is not a family, and
 * accepting it would put an empty entry in a list that later code reads as a
 * name.
 */
static robo_result_t computed_compute_family(robo_css_computed_value_t *out,
                                             const robo_css_value_t *specified,
                                             robo_css_diag_t *diag)
{
    size_t families = 0u;
    size_t total = 0u;
    size_t index;
    int expecting_part = 1;
    char *joined;
    size_t position = 0u;
    robo_result_t result;

    if (!specified || specified->kind != ROBO_CSS_VALUE_KIND_LIST) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                               "not a family list", diag);
    }
    for (index = 0u; index < specified->children.count; index++) {
        const robo_css_value_t *child = &specified->children.items[index];
        size_t length;

        if (computed_is_trivia(child)) {
            continue;
        }
        if (child->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                   "not a family name", diag);
        }
        if (child->token.type == ROBO_CSS_TOKEN_COMMA) {
            if (expecting_part) {
                return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                       "a family list cannot begin with a comma", diag);
            }
            expecting_part = 1;
            continue;
        }
        if (child->token.type != ROBO_CSS_TOKEN_IDENT &&
            child->token.type != ROBO_CSS_TOKEN_STRING) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                   "not a family name", diag);
        }
        if (!expecting_part) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                   "two family names with no comma between them", diag);
        }
        length = robo_css_token_value_length(&child->token);
        if (length == 0u) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                   "an empty family name is not a family", diag);
        }
        if (total > (size_t)-1 - length) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                                 "the family list is absurdly long");
        }
        total += length;
        families++;
        expecting_part = 0;
    }
    if (families == 0u || expecting_part) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                               "not a family list", diag);
    }

    /* `, ` between families, so the value is a list rather than a coincidence of
     * token order. */
    joined = (char *)robo_malloc(total + (families - 1u) * 2u + 1u);
    if (!joined) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                             "could not build the family list");
    }
    for (index = 0u; index < specified->children.count; index++) {
        const robo_css_value_t *child = &specified->children.items[index];
        size_t length = 0u;
        uint8_t scratch[256];
        uint8_t *bytes = scratch;

        if (computed_is_trivia(child) || child->token.type == ROBO_CSS_TOKEN_COMMA) {
            if (child->kind == ROBO_CSS_VALUE_KIND_TOKEN &&
                child->token.type == ROBO_CSS_TOKEN_COMMA) {
                joined[position] = ',';
                joined[position + 1u] = ' ';
                position += 2u;
            }
            continue;
        }
        length = robo_css_token_value_length(&child->token);
        if (length > sizeof(scratch)) {
            /* The first pass proved a family this long exists; a list long
             * enough to need the heap is rare, so it is handled rather than
             * rejected, but the stack path is the one that runs. */
            bytes = (uint8_t *)robo_malloc(length);
            if (!bytes) {
                robo_free(joined);
                return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                                     "could not build the family list");
            }
        }
        /* The *decoded* value, so `\31 a` and `1a` are the same family. */
        (void)robo_css_token_value_copy(&child->token, bytes, length, &length, NULL);
        memcpy(joined + position, bytes, length);
        position += length;
        if (bytes != scratch) {
            robo_free(bytes);
        }
    }
    joined[position] = '\0';
    result = robo_css_computed_value_make_family_list(out, robo_css_slice_from_text(joined),
                                                      diag);
    robo_free(joined);
    return result;
}

/** Compute `normal`/`bold`/…/100–900. */
static robo_result_t computed_compute_font_weight(robo_css_computed_value_t *out,
                                                  const robo_css_value_t *specified,
                                                  robo_css_diag_t *diag)
{
    const robo_css_token_t *token = computed_single_token(specified);

    if (!token) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,
                               "not a font weight", diag);
    }
    if (token->type == ROBO_CSS_TOKEN_NUMBER) {
        if (!computed_number_is_finite(token->number) || token->number < 1.0 ||
            token->number > 1000.0) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,
                                   "a font weight number must be 1 to 1000", diag);
        }
        return robo_css_computed_value_make_number(out, token->number,
                                                   token->is_integer, diag);
    }
    if (token->type == ROBO_CSS_TOKEN_IDENT) {
        static const char *const weights[] = { "normal", "bold", "bolder", "lighter" };
        size_t index;

        for (index = 0u; index < sizeof(weights) / sizeof(weights[0]); index++) {
            if (robo_css_token_value_equals_ascii_ci(token, weights[index])) {
                return robo_css_computed_value_make_keyword(
                    out, robo_css_slice_from_text(weights[index]), diag);
            }
        }
    }
    return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,
                           "not a font weight", diag);
}

/** Compute `normal`, a number, a length or a percentage. */
static robo_result_t computed_compute_line_height(robo_css_computed_value_t *out,
                                                  const robo_css_value_t *specified,
                                                  robo_css_diag_t *diag)
{
    const robo_css_token_t *token = computed_single_token(specified);

    if (token && token->type == ROBO_CSS_TOKEN_IDENT &&
        robo_css_token_value_equals_ascii_ci(token, "normal")) {
        return robo_css_computed_value_make_keyword(out, robo_css_slice_from_text("normal"),
                                                    diag);
    }
    if (token && token->type == ROBO_CSS_TOKEN_NUMBER) {
        if (!computed_number_is_finite(token->number)) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT,
                                   "not a finite number", diag);
        }
        /* A bare number is a *multiplier* here, not a length. */
        return robo_css_computed_value_make_number(out, token->number,
                                                   token->is_integer, diag);
    }
    return computed_compute_length(out, specified, diag);
}

robo_result_t robo_css_computed_value_compute(
    robo_css_property_computation_t computation,
    const robo_css_value_t *specified,
    robo_css_computed_value_t *out,
    robo_css_diag_t *diag)
{
    if (!out || !specified) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "output or specified value is NULL");
    }
    robo_css_computed_value_init(out);
    if (!robo_css_property_computation_is_known(computation)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "not a value-computation classification");
    }
    if (specified->kind == ROBO_CSS_VALUE_KIND_NONE) {
        /* Nothing was specified: NONE, and not an invalid value. The entry this
         * belongs to carries the "unresolved default" fact, not the value. */
        return ROBO_OK;
    }

    switch (computation) {
    case ROBO_CSS_PROPERTY_COMPUTATION_COLOR:
        return computed_compute_color(out, specified, diag);
    case ROBO_CSS_PROPERTY_COMPUTATION_LENGTH:
        return computed_compute_length(out, specified, diag);
    case ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD:
        return computed_compute_keyword(out, specified, diag);
    case ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY:
        return computed_compute_family(out, specified, diag);
    case ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT:
        return computed_compute_font_weight(out, specified, diag);
    case ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT:
        return computed_compute_line_height(out, specified, diag);
    default:
        return computed_keep(out, specified, computed_function_is_unresolved(
                                             computed_single(specified)),
                             diag);
    }
}
