/*
 * RoboCSS colours (Phase 9.9) — see RoboCSSColor.h.
 *
 * Four things live here and nothing else:
 *
 *   records     the four kinds, their names, equality, and the constructors
 *   the set     the 148 standard named colours, by name and by index
 *   parsing     hex, `rgb()`/`rgba()`, `hsl()`/`hsla()`, and the dispatch that
 *               knows which of those a function is
 *   contextual  `currentColor`'s substitution, and the one spelling each kind
 *               serializes as
 *
 * Two habits are worth naming because they are what keeps this file honest.
 *
 * First, every path that reads a token reads its *value* rather than its `text`:
 * `R\47 B` is `rgb`, `\72 ed` is `red`, and a hex digit cannot be an escape
 * because escapes were resolved by the time this file looks. Using the raw bytes
 * would make an escape spelling a different colour, which is exactly the bug
 * CSS's escape rule exists to prevent.
 *
 * Second, nothing here guesses. An unknown identifier is not a colour, a
 * malformed call is not a colour, a colour space this build does not represent
 * is *reported as such* rather than approximated, and `currentColor` is never
 * substituted with a default. Every one of those is a case where a plausible
 * answer would be worse than a refusal, because a plausible wrong colour is
 * indistinguishable from a right one at the point of use.
 */

#include "RoboCSS/RoboCSSColor.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

int robo_css_color_kind_is_known(robo_css_color_kind_t kind)
{
    return kind >= ROBO_CSS_COLOR_NONE &&
           kind < (robo_css_color_kind_t)ROBO_CSS_COLOR_KIND_COUNT;
}

const char *robo_css_color_kind_name(robo_css_color_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_COLOR_NONE:
        return "none";
    case ROBO_CSS_COLOR_RGBA:
        return "rgba";
    case ROBO_CSS_COLOR_CURRENT:
        return "current";
    case ROBO_CSS_COLOR_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

/** A record of @p kind with zeroed channels. */
static robo_css_color_t color_of_kind(robo_css_color_kind_t kind)
{
    robo_css_color_t color;

    color.kind = kind;
    color.red = 0u;
    color.green = 0u;
    color.blue = 0u;
    color.alpha = 0u;
    return color;
}

robo_css_color_t robo_css_color_none(void)
{
    return color_of_kind(ROBO_CSS_COLOR_NONE);
}

robo_css_color_t robo_css_color_invalid(void)
{
    return color_of_kind(ROBO_CSS_COLOR_INVALID);
}

robo_css_color_t robo_css_color_current(void)
{
    return color_of_kind(ROBO_CSS_COLOR_CURRENT);
}

robo_css_color_t robo_css_color_make(uint8_t red, uint8_t green, uint8_t blue,
                                     uint8_t alpha)
{
    robo_css_color_t color;

    color.kind = ROBO_CSS_COLOR_RGBA;
    color.red = red;
    color.green = green;
    color.blue = blue;
    color.alpha = alpha;
    return color;
}

/** A fully opaque named colour from a 24-bit RGB triple. */
static robo_css_color_t color_of_rgb24(uint32_t rgb)
{
    return robo_css_color_make((uint8_t)((rgb >> 16) & 0xffu),
                               (uint8_t)((rgb >> 8) & 0xffu),
                               (uint8_t)(rgb & 0xffu), 255u);
}

int robo_css_color_is_resolved(robo_css_color_t color)
{
    return color.kind == ROBO_CSS_COLOR_RGBA;
}

int robo_css_color_is_current(robo_css_color_t color)
{
    return color.kind == ROBO_CSS_COLOR_CURRENT;
}

int robo_css_color_is_valid(robo_css_color_t color)
{
    return color.kind == ROBO_CSS_COLOR_RGBA || color.kind == ROBO_CSS_COLOR_CURRENT;
}

int robo_css_color_equals(robo_css_color_t a, robo_css_color_t b)
{
    if (a.kind != b.kind) {
        return 0;
    }
    if (a.kind != ROBO_CSS_COLOR_RGBA) {
        /* Two NOTHINGs are equal and two INVALIDs are equal; two CURRENTS are
         * equal because there is only one keyword. The channels of a record that
         * is not a colour are never read, which is what makes them free to be
         * anything. */
        return 1;
    }
    return a.red == b.red && a.green == b.green && a.blue == b.blue && a.alpha == b.alpha;
}

/* ------------------------------------------------------------------ */
/* The standard named colours                                          */
/* ------------------------------------------------------------------ */

/** One named colour, as a 24-bit RGB triple. */
typedef struct color_named_entry {
    const char *name;
    uint32_t rgb;
} color_named_entry_t;

/*
 * The standard set, and it is complete rather than representative.
 *
 * Every name CSS defines: the 16 CSS2.1 keywords, the 130 colour names SVG
 * shares with X11 — duplicates included, because `gray` and `grey` are two names
 * and both are legal — and `rebeccapurple`, which CSS added to the standard set.
 * A table this size is why the search is a linear scan rather than something
 * cleverer: 148 case-insensitive comparisons happen once per declaration at
 * compute time, and an index that could disagree with the table it indexes is a
 * worse trade than a loop.
 *
 * The order is alphabetical, which is not semantically meaningful, and is
 * deliberate anyway: the table is read by human beings far more often than by
 * the loop, and `named_at` exists so a suite can walk the whole set — including
 * the entries nobody thought to type a test for — rather than trusting that the
 * names someone remembered are the names that are there.
 */
static const color_named_entry_t color_named_entries[] = {
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

#define COLOR_NAMED_COUNT \
    (sizeof(color_named_entries) / sizeof(color_named_entries[0]))

size_t robo_css_color_named_count(void)
{
    return COLOR_NAMED_COUNT;
}

int robo_css_color_named_at(size_t index, robo_css_slice_t *out_name,
                            robo_css_color_t *out_color)
{
    if (index >= COLOR_NAMED_COUNT) {
        return 0;
    }
    if (out_name) {
        *out_name = robo_css_slice_from_text(color_named_entries[index].name);
    }
    if (out_color) {
        *out_color = color_of_rgb24(color_named_entries[index].rgb);
    }
    return 1;
}

int robo_css_color_from_name(robo_css_slice_t name, robo_css_color_t *out)
{
    size_t index;

    if (!out || robo_css_slice_is_absent(name) || name.length == 0u) {
        return 0;
    }
    for (index = 0u; index < COLOR_NAMED_COUNT; index++) {
        if (!robo_css_bytes_equal_ascii_ci(name.bytes, name.length,
                                           color_named_entries[index].name)) {
            continue;
        }
        *out = color_of_rgb24(color_named_entries[index].rgb);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Hex                                                                 */
/* ------------------------------------------------------------------ */

/** The value a hex digit spells, or -1 when the byte is not one. */
static int color_hex_digit(uint8_t byte)
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
    /* CSS's four hex forms and nothing else. A length that is not one of them is
     * refused rather than padded or truncated: `#ff` read as a red is a colour
     * nobody wrote. */
    if (digits.length != 3u && digits.length != 4u && digits.length != 6u &&
        digits.length != 8u) {
        return 0;
    }
    for (index = 0u; index < digits.length; index++) {
        int value = color_hex_digit(digits.bytes[index]);

        if (value < 0) {
            return 0;
        }
        high[index] = value;
        low[index] = value;
    }

    switch (digits.length) {
    case 3u:
        /* `#rgb` is each nibble *doubled*, which is not the same as a
         * zero-padded byte: `#f00` is (0xff, 0x00, 0x00), and reading it as
         * (0x0f, 0x00, 0x00) is the classic three-digit mistake. */
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
/* Function arguments                                                  */
/* ------------------------------------------------------------------ */

/*
 * One argument list, two syntaxes, one walker.
 *
 * `rgb()` and `hsl()` take the same *shape* of argument list — three components
 * and an optional alpha, separated by commas or by spaces with the alpha behind
 * a `/` — and differ only in what each component means. So the walker is shared
 * and the meaning is not: this section produces arguments, and the two parsers
 * below decide what they may be.
 *
 * The one rule the walker does enforce is CSS's: the two syntaxes do not mix.
 * The separators are tracked as a *style*, and a second style is refused, which
 * is what makes `rgb(255, 0 0)` malformed rather than "close enough".
 * Whitespace touching a comma is not a separator, which is why `rgb(1 , 2 , 3)`
 * is legal: the comma separates and the space around it is decoration.
 */

/** One argument as it was written. */
typedef struct color_argument {
    double value;
    /** Non-zero when the token was a percentage. */
    int is_percentage;
    /** DIMENSION: the unit as written; absent for a number or a percentage. */
    robo_css_slice_t unit;
} color_argument_t;

/** Which of the two argument-list syntaxes a call used. */
typedef enum color_list_style {
    COLOR_LIST_STYLE_NONE = 0,
    COLOR_LIST_STYLE_COMMA = 1,
    COLOR_LIST_STYLE_SPACE = 2
} color_list_style_t;

/**
 * Split a colour function's children into arguments.
 *
 * The children arrive with their whitespace tokens intact (the grammar trims a
 * value's ends, not its interior), so this walks the significant components and
 * reads what stands between the arguments. Numbers, percentages and dimensions
 * are all accepted — whether a dimension may appear is each parser's rule, since
 * only the hue takes one.
 *
 * `/` is the modern syntax's alpha separator: it is only reachable from the
 * space style, so `rgb(1, 2, 3 / 4)` never gets past its slash.
 *
 * @return non-zero when the list is well formed. The count, the style and
 *         whether a slash was seen are what tell the caller which shape it is;
 *         none of them is checked against what the *function name* means here,
 *         because both spellings accept both shapes and each parser owns its own
 *         rule.
 */
static int color_split(const robo_css_value_t *function,
                       color_argument_t *arguments,
                       size_t argument_capacity,
                       size_t *out_count,
                       color_list_style_t *out_style,
                       int *out_saw_slash)
{
    color_list_style_t style = COLOR_LIST_STYLE_NONE;
    size_t count = 0u;
    int saw_slash = 0;
    int pending_comma = 0;
    int pending_space = 0;
    int after_slash = 0;
    int last_was_argument = 0;
    size_t index;

    *out_count = 0u;
    *out_style = COLOR_LIST_STYLE_NONE;
    *out_saw_slash = 0;
    for (index = 0u; index < function->children.count; index++) {
        const robo_css_value_t *child = &function->children.items[index];

        if (robo_css_value_is_trivia(child)) {
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
            if (!last_was_argument || saw_slash || style == COLOR_LIST_STYLE_COMMA) {
                return 0;
            }
            style = COLOR_LIST_STYLE_SPACE;
            saw_slash = 1;
            after_slash = 1;
            pending_comma = 0;
            pending_space = 0;
            last_was_argument = 0;
            continue;
        case ROBO_CSS_TOKEN_NUMBER:
        case ROBO_CSS_TOKEN_PERCENTAGE:
        case ROBO_CSS_TOKEN_DIMENSION:
            break;
        default:
            return 0;
        }
        if (count > 0u && !after_slash) {
            /* Every argument after the first needs a separator, and it has to
             * be the same one the call started with. */
            color_list_style_t separator = pending_comma ? COLOR_LIST_STYLE_COMMA
                                                         : COLOR_LIST_STYLE_SPACE;

            if (!pending_comma && !pending_space) {
                return 0;
            }
            if (style == COLOR_LIST_STYLE_NONE) {
                style = separator;
            } else if (style != separator) {
                return 0;
            }
        }
        after_slash = 0;
        if (count >= argument_capacity) {
            return 0;
        }
        if (!robo_css_number_is_finite(child->token.number)) {
            return 0;
        }
        arguments[count].value = child->token.number;
        arguments[count].is_percentage =
            child->token.type == ROBO_CSS_TOKEN_PERCENTAGE;
        arguments[count].unit = child->token.type == ROBO_CSS_TOKEN_DIMENSION
                                    ? child->token.unit
                                    : robo_css_slice_empty();
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
    *out_style = style;
    *out_saw_slash = saw_slash;
    return 1;
}

/** 0–255 channel for a 0–255 number, clamped and rounded. */
static uint8_t color_channel_255(double value)
{
    return (uint8_t)robo_css_number_round(robo_css_number_clamp(value, 0.0, 255.0));
}

/** 0–255 channel for a 0–100 percentage, clamped and rounded. */
static uint8_t color_channel_percent(double value)
{
    return (uint8_t)robo_css_number_round(robo_css_number_clamp(value, 0.0, 100.0) *
                                          255.0 / 100.0);
}

/** 0–255 alpha for a 0–1 number, clamped and rounded. */
static uint8_t color_alpha_number(double value)
{
    return (uint8_t)robo_css_number_round(robo_css_number_clamp(value, 0.0, 1.0) * 255.0);
}

/** 0–255 alpha for a 0–100 percentage, clamped and rounded. */
static uint8_t color_alpha_percent(double value)
{
    return (uint8_t)robo_css_number_round(robo_css_number_clamp(value, 0.0, 100.0) *
                                          255.0 / 100.0);
}

/** One RGB channel in 0–255, from a number or a percentage. */
static uint8_t color_rgb_channel(color_argument_t argument)
{
    return argument.is_percentage ? color_channel_percent(argument.value)
                                  : color_channel_255(argument.value);
}

/** One alpha in 0–255, from a number or a percentage. */
static uint8_t color_alpha_of(color_argument_t argument)
{
    return argument.is_percentage ? color_alpha_percent(argument.value)
                                  : color_alpha_number(argument.value);
}

int robo_css_color_from_rgb_function(const robo_css_value_t *function,
                                     robo_css_color_t *out)
{
    color_argument_t arguments[4];
    size_t count = 0u;
    color_list_style_t style = COLOR_LIST_STYLE_NONE;
    int saw_slash = 0;

    if (!function || !out || function->kind != ROBO_CSS_VALUE_KIND_FUNCTION) {
        return 0;
    }
    if (!robo_css_value_function_named(function, "rgb") &&
        !robo_css_value_function_named(function, "rgba")) {
        return 0;
    }
    if (!color_split(function, arguments, 4u, &count, &style, &saw_slash)) {
        return 0;
    }
    if (count != 3u && count != 4u) {
        return 0;
    }
    /* An RGB channel is a number or a percentage, never a dimension: `rgb(10px
     * 0 0)` is a mistake, and a length silently read as a channel is exactly the
     * kind of guess this file does not make. */
    {
        size_t index;

        for (index = 0u; index < count; index++) {
            if (!robo_css_slice_is_absent(arguments[index].unit)) {
                return 0;
            }
        }
    }
    /* A fourth argument is always the alpha, and it is only written one of two
     * ways: after the legacy comma, or after the modern slash. Four numbers
     * separated by nothing but spaces — `rgb(1 2 3 4)` — has neither, and is a
     * malformed call rather than an opaque one. */
    if (count == 4u && !saw_slash && style != COLOR_LIST_STYLE_COMMA) {
        return 0;
    }
    if (saw_slash && count != 4u) {
        return 0;
    }
    /* A four-argument comma call is the legacy `rgba()`, and a three-argument
     * one is fully opaque. Both spellings of the function name accept both
     * shapes in CSS Color 4, so nothing here distinguishes `rgb` from `rgba`. */
    *out = robo_css_color_make(color_rgb_channel(arguments[0]),
                               color_rgb_channel(arguments[1]),
                               color_rgb_channel(arguments[2]),
                               count == 4u ? color_alpha_of(arguments[3]) : 255u);
    return 1;
}

/* ------------------------------------------------------------------ */
/* hsl() / hsla()                                                      */
/* ------------------------------------------------------------------ */

/*
 * Hue is an angle, and an angle is a number with one of four units.
 *
 * `hsl(0.25turn 50% 50%)` is ordinary authoring, so refusing the unit forms
 * would be a limitation rather than a decision. The four factors are CSS's own,
 * and they are exact by definition: a turn is 360 degrees, a gradian is 9/10 of
 * one, and a radian is 180/pi.
 */
typedef struct color_angle_unit {
    const char *name;
    double to_degrees;
} color_angle_unit_t;

static const color_angle_unit_t color_angle_units[] = {
    { "deg", 1.0 },
    { "grad", 360.0 / 400.0 },
    { "rad", 180.0 / 3.14159265358979323846 },
    { "turn", 360.0 }
};

#define COLOR_ANGLE_COUNT (sizeof(color_angle_units) / sizeof(color_angle_units[0]))

/**
 * One hue argument in degrees, or 0 with *ok cleared when it is not an angle.
 *
 * A bare number is degrees. A dimension is degrees only if its unit is one of
 * the four angle units: `hsl(10px 50% 50%)` is not a hue, and reading the 10 as
 * degrees would be a colour nobody wrote.
 */
static double color_hue_degrees(color_argument_t argument, int *ok)
{
    size_t index;

    *ok = 1;
    if (robo_css_slice_is_absent(argument.unit) || argument.unit.length == 0u) {
        if (argument.is_percentage) {
            *ok = 0;
            return 0.0;
        }
        return argument.value;
    }
    for (index = 0u; index < COLOR_ANGLE_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(argument.unit.bytes, argument.unit.length,
                                          color_angle_units[index].name)) {
            return argument.value * color_angle_units[index].to_degrees;
        }
    }
    *ok = 0;
    return 0.0;
}

/**
 * @p value reduced into [0, 360) degrees.
 *
 * Hue is *normalised* rather than clamped — `-120` and `240` are the same hue,
 * and `720` is `0` — which is CSS's rule and the reason a wheel can be written
 * either way round. The reduction is done in doubles by hand rather than with
 * `fmod`, because this runtime is freestanding: there is no math library to call.
 *
 * A magnitude at or above 2^52 degrees saturates to 0. That is a *documented
 * bound*, not a rounding: past it a double has no fractional part left to reduce
 * with, so any value produced from one would be an artefact of the representation
 * rather than the author's angle. A hue that large is not a hue anybody wrote,
 * and pinning the answer is better than pinning a different one per platform.
 *
 * A non-finite angle (the tokenizer can produce an infinity from a huge literal)
 * has no hue either, and answers the same 0 rather than propagating a NaN into
 * four channels.
 */
static double color_mod_360(double value)
{
    double turns;

    if (!robo_css_number_is_finite(value)) {
        return 0.0;
    }
    if (value >= 4503599627370496.0 || value <= -4503599627370496.0) {
        return 0.0;
    }
    turns = value / 360.0;
    /* Truncation toward zero, then the fraction back in degrees: the sign is
     * handled by the final correction rather than by branching here. */
    turns = (turns - (double)(long long)turns) * 360.0;
    if (turns < 0.0) {
        turns += 360.0;
    }
    return turns;
}

/** A 0–1 saturation or lightness from a percentage, clamped. */
static double color_ratio_percent(double value)
{
    return robo_css_number_clamp(value, 0.0, 100.0) / 100.0;
}

/**
 * One component of CSS Color 4's `hslToRgb`.
 *
 * `n` names which of the three channels is being computed (0, 4 and 8 for red,
 * green and blue), and the wrap is the specification's own `(n + h/30) mod 12`.
 * Written as the spec writes it because the alternative — the six-sector
 * formulation — is the one that gets a sector boundary wrong.
 *
 * `hue` is already in [0, 360) and `n` is at most 8, so `k` is in [0, 20) and
 * one subtraction is the whole of the wrap. The subtraction is written as a
 * loop-shaped `if` with the bound stated rather than as an unconditional minus,
 * so that a future caller with a wider hue cannot silently produce a `k` that
 * indexes a sector that does not exist.
 */
static double color_hsl_component(double n, double hue, double alpha, double lightness)
{
    double k = n + hue / 30.0;
    double low;

    if (k >= 12.0) {
        k -= 12.0;
    }
    low = k - 3.0;
    if (low > 9.0 - k) {
        low = 9.0 - k;
    }
    if (low > 1.0) {
        low = 1.0;
    }
    if (low < -1.0) {
        low = -1.0;
    }
    return lightness - alpha * low;
}

/** A channel in 0–1 from a 0–1 double, rounded to the 8-bit grid. */
static uint8_t color_hsl_channel(double unit_value)
{
    return (uint8_t)robo_css_number_round(robo_css_number_clamp(unit_value, 0.0, 1.0) *
                                          255.0);
}

int robo_css_color_from_hsl_function(const robo_css_value_t *function,
                                     robo_css_color_t *out)
{
    color_argument_t arguments[4];
    size_t count = 0u;
    color_list_style_t style = COLOR_LIST_STYLE_NONE;
    int saw_slash = 0;
    int hue_ok = 0;
    double hue;
    size_t index;

    if (!function || !out || function->kind != ROBO_CSS_VALUE_KIND_FUNCTION) {
        return 0;
    }
    if (!robo_css_value_function_named(function, "hsl") &&
        !robo_css_value_function_named(function, "hsla")) {
        return 0;
    }
    if (!color_split(function, arguments, 4u, &count, &style, &saw_slash)) {
        return 0;
    }
    if (count != 3u && count != 4u) {
        return 0;
    }
    if (count == 4u && !saw_slash && style != COLOR_LIST_STYLE_COMMA) {
        return 0;
    }
    if (saw_slash && count != 4u) {
        return 0;
    }
    hue = color_hue_degrees(arguments[0], &hue_ok);
    if (!hue_ok) {
        return 0;
    }
    /* Saturation and lightness are percentages in the legacy comma syntax, which
     * is CSS's own rule: `hsl(120, 50, 50)` is not the legacy spelling of
     * anything. The modern syntax also accepts bare numbers, which CSS Color 4
     * reads as percentages — so `hsl(120 50 50)` is a colour and
     * `hsl(120, 50%, 50%)` is the same colour. Neither may carry a unit: a
     * saturation of `10px` is a mistake, not a saturation. */
    for (index = 1u; index < 3u; index++) {
        if (!robo_css_slice_is_absent(arguments[index].unit)) {
            return 0;
        }
        if (!arguments[index].is_percentage && style == COLOR_LIST_STYLE_COMMA) {
            return 0;
        }
    }
    hue = color_mod_360(hue);
    {
        double saturation = color_ratio_percent(arguments[1].value);
        double lightness = color_ratio_percent(arguments[2].value);
        double amount = saturation * (lightness < 1.0 - lightness ? lightness
                                                                 : 1.0 - lightness);
        uint8_t alpha = count == 4u ? color_alpha_of(arguments[3]) : 255u;
        /* The conversion runs in doubles once, and the 8-bit rounding happens
         * here — at the end, not at every intermediate step. That is what makes
         * `hsl(120 100% 25%)` exactly `#008000` rather than one channel short of
         * it. */
        uint8_t red = color_hsl_channel(color_hsl_component(0.0, hue, amount, lightness));
        uint8_t green = color_hsl_channel(color_hsl_component(8.0, hue, amount, lightness));
        uint8_t blue = color_hsl_channel(color_hsl_component(4.0, hue, amount, lightness));

        *out = robo_css_color_make(red, green, blue, alpha);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The dispatch                                                        */
/* ------------------------------------------------------------------ */

int robo_css_color_parse_is_known(robo_css_color_parse_t outcome)
{
    return outcome >= ROBO_CSS_COLOR_PARSE_INVALID &&
           outcome < (robo_css_color_parse_t)ROBO_CSS_COLOR_PARSE_COUNT;
}

const char *robo_css_color_parse_name(robo_css_color_parse_t outcome)
{
    switch (outcome) {
    case ROBO_CSS_COLOR_PARSE_INVALID:
        return "invalid";
    case ROBO_CSS_COLOR_PARSE_RESOLVED:
        return "resolved";
    case ROBO_CSS_COLOR_PARSE_CURRENT:
        return "current";
    case ROBO_CSS_COLOR_PARSE_DEFERRED:
        return "deferred";
    case ROBO_CSS_COLOR_PARSE_MALFORMED:
        return "malformed";
    case ROBO_CSS_COLOR_PARSE_UNKNOWN_NAME:
        return "unknown-name";
    default:
        return "unknown";
    }
}

/*
 * The colour functions this build recognises and cannot represent.
 *
 * Two lists, not one, and the distinction is the point. These are **legal CSS**
 * in a colour space this build has no representation for: an author who writes
 * `oklch(0.7 0.1 200)` wrote a colour, and telling them it is invalid would be a
 * lie about their stylesheet. So the value is carried (the computed-value layer
 * keeps the tokens and flags it unresolved) and the caller is told DEFERRED.
 *
 * A function that is *not* on this list and is not `rgb`/`hsl` is a different
 * thing entirely — `sparkle(1)` is not a colour function at all — and it is
 * refused.
 */
static const char *const color_unrepresentable_functions[] = {
    "hwb", "lab", "lch", "oklab", "oklch", "color", "color-mix", "device-cmyk",
    "light-dark"
};

int robo_css_color_function_is_unrepresentable(robo_css_slice_t name)
{
    size_t index;

    if (robo_css_slice_is_absent(name) || name.length == 0u) {
        return 0;
    }
    for (index = 0u;
         index < sizeof(color_unrepresentable_functions) /
                     sizeof(color_unrepresentable_functions[0]);
         index++) {
        if (robo_css_bytes_equal_ascii_ci(name.bytes, name.length,
                                          color_unrepresentable_functions[index])) {
            return 1;
        }
    }
    return 0;
}

robo_css_color_parse_t robo_css_color_from_function(const robo_css_value_t *function,
                                                    robo_css_color_t *out)
{
    robo_css_color_t parsed;

    if (!out) {
        return ROBO_CSS_COLOR_PARSE_INVALID;
    }
    if (!function || function->kind != ROBO_CSS_VALUE_KIND_FUNCTION) {
        return ROBO_CSS_COLOR_PARSE_INVALID;
    }
    if (robo_css_color_from_rgb_function(function, &parsed)) {
        *out = parsed;
        return ROBO_CSS_COLOR_PARSE_RESOLVED;
    }
    if (robo_css_color_from_hsl_function(function, &parsed)) {
        *out = parsed;
        return ROBO_CSS_COLOR_PARSE_RESOLVED;
    }
    /* Not a colour after both parsers refused it. Why they refused it is what
     * decides the report: a colour function written wrong is a mistake to point
     * at, and a colour space this build does not have is not a mistake at all. */
    if (robo_css_value_function_named(function, "rgb") ||
        robo_css_value_function_named(function, "rgba") ||
        robo_css_value_function_named(function, "hsl") ||
        robo_css_value_function_named(function, "hsla")) {
        return ROBO_CSS_COLOR_PARSE_MALFORMED;
    }
    if (robo_css_color_function_is_unrepresentable(function->name)) {
        return ROBO_CSS_COLOR_PARSE_DEFERRED;
    }
    return ROBO_CSS_COLOR_PARSE_INVALID;
}

/**
 * The colour a `#…` hash token spells, or NONE.
 *
 * The token's *value* is what is read (the tokenizer strips the `#` and resolves
 * escapes), and it is copied into a local buffer because the value accessors do
 * not hand out a slice. 16 bytes is comfortably more than the eight hex digits
 * the longest form has; a longer hash is refused before the copy rather than
 * truncated into a colour nobody wrote.
 */
static robo_css_color_parse_t color_from_hash_token(const robo_css_token_t *token,
                                                    robo_css_color_t *out)
{
    uint8_t digits[16];
    size_t length = 0u;

    if (robo_css_token_value_length(token) > sizeof(digits) ||
        robo_css_token_value_copy(token, digits, sizeof(digits), &length, NULL) != ROBO_OK) {
        return ROBO_CSS_COLOR_PARSE_MALFORMED;
    }
    if (!robo_css_color_from_hex(robo_css_slice_from_bytes(digits, length), out)) {
        return ROBO_CSS_COLOR_PARSE_MALFORMED;
    }
    return ROBO_CSS_COLOR_PARSE_RESOLVED;
}

robo_css_color_parse_t robo_css_color_from_value(const robo_css_value_t *specified,
                                                 robo_css_color_t *out)
{
    const robo_css_value_t *single;
    char keyword[64];
    size_t length = 0u;
    robo_css_slice_t name;

    if (!out) {
        return ROBO_CSS_COLOR_PARSE_INVALID;
    }
    single = robo_css_value_single(specified);
    if (!single) {
        /* Nothing, or more than one component: `red blue` is two colours and
         * therefore not a colour. */
        return ROBO_CSS_COLOR_PARSE_INVALID;
    }
    if (single->kind == ROBO_CSS_VALUE_KIND_FUNCTION) {
        return robo_css_color_from_function(single, out);
    }
    if (single->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        return ROBO_CSS_COLOR_PARSE_INVALID;
    }
    if (single->token.type == ROBO_CSS_TOKEN_HASH) {
        return color_from_hash_token(&single->token, out);
    }
    if (single->token.type != ROBO_CSS_TOKEN_IDENT) {
        /* A number, a string, a dimension: none of them is a colour. */
        return ROBO_CSS_COLOR_PARSE_INVALID;
    }
    if (!robo_css_token_keyword_text(&single->token, keyword, sizeof(keyword), &length)) {
        return ROBO_CSS_COLOR_PARSE_MALFORMED;
    }
    name = robo_css_slice_from_bytes((const uint8_t *)keyword, length);
    if (robo_css_color_from_name(name, out)) {
        return ROBO_CSS_COLOR_PARSE_RESOLVED;
    }
    if (robo_css_slice_equals_ascii(name, "transparent")) {
        /* A colour (the transparent black) but not a *named* one, so it is
         * answered here rather than in the table. */
        *out = robo_css_color_make(0u, 0u, 0u, 0u);
        return ROBO_CSS_COLOR_PARSE_RESOLVED;
    }
    if (robo_css_slice_equals_ascii(name, "currentcolor")) {
        /* Recognised, typed, and *not* resolved: its value is the element's own
         * computed `color`, which no value-layer context can supply. */
        *out = robo_css_color_current();
        return ROBO_CSS_COLOR_PARSE_CURRENT;
    }
    /* An identifier that is not a colour keyword. Reported apart from INVALID
     * because the author misspelled a colour rather than put the wrong *kind*
     * of value in a colour property, and a message can say which. */
    return ROBO_CSS_COLOR_PARSE_UNKNOWN_NAME;
}

/* ------------------------------------------------------------------ */
/* Contextual resolution                                               */
/* ------------------------------------------------------------------ */

int robo_css_color_resolve_current(robo_css_color_t color, robo_css_color_t current_color,
                                   robo_css_color_t *out)
{
    robo_css_color_t resolved = color;

    if (color.kind == ROBO_CSS_COLOR_CURRENT && current_color.kind == ROBO_CSS_COLOR_RGBA) {
        resolved = current_color;
    }
    if (out) {
        *out = resolved;
    }
    return resolved.kind == ROBO_CSS_COLOR_RGBA;
}

/* ------------------------------------------------------------------ */
/* Serialization                                                       */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_color_write(robo_css_color_t color, char *buffer, size_t capacity,
                                   size_t *out_length, robo_css_diag_t *diag)
{
    char text[ROBO_CSS_COLOR_TEXT_MAX];
    size_t length;

    if (!buffer || !out_length) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "buffer and length output are required");
    }
    if (!robo_css_color_kind_is_known(color.kind)) {
        *out_length = 0u;
        buffer[0] = '\0';
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "the colour's kind is not one this build defines");
    }
    switch (color.kind) {
    case ROBO_CSS_COLOR_NONE:
        length = (size_t)snprintf(text, sizeof(text), "none");
        break;
    case ROBO_CSS_COLOR_INVALID:
        length = (size_t)snprintf(text, sizeof(text), "invalid");
        break;
    case ROBO_CSS_COLOR_CURRENT:
        /* One spelling for one keyword, and this is CSS's: the camel-case
         * `currentColor`, not a lowercased rendering of how it was typed. */
        length = (size_t)snprintf(text, sizeof(text), "currentColor");
        break;
    default:
        length = (size_t)snprintf(text, sizeof(text),
                                  color.alpha == 255u ? "#%02x%02x%02x"
                                                      : "#%02x%02x%02x%02x",
                                  (unsigned)color.red, (unsigned)color.green,
                                  (unsigned)color.blue, (unsigned)color.alpha);
        break;
    }
    *out_length = length;
    if (length + 1u > capacity) {
        buffer[0] = '\0';
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED,
                             "the colour's text does not fit");
    }
    memcpy(buffer, text, length + 1u);
    return ROBO_OK;
}

