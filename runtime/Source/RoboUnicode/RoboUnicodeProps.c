/*
 * RoboUnicodeProps — thin public wrappers over generated UCD tables.
 */

#include "RoboUnicode/RoboUnicodeProps.h"

robo_unicode_general_category_t robo_unicode_category(robo_uchar32_t cp)
{
    return robo_ucd_general_category(cp);
}

robo_unicode_script_t robo_unicode_script(robo_uchar32_t cp)
{
    return robo_ucd_script(cp);
}

unsigned robo_unicode_combining_class(robo_uchar32_t cp)
{
    return robo_ucd_combining_class(cp);
}

robo_unicode_east_asian_width_t robo_unicode_east_asian_width(robo_uchar32_t cp)
{
    return robo_ucd_east_asian_width(cp);
}

int robo_unicode_is_whitespace(robo_uchar32_t cp)
{
    return robo_ucd_is_white_space(cp);
}

int robo_unicode_is_alphabetic(robo_uchar32_t cp)
{
    return robo_ucd_is_alphabetic(cp);
}

int robo_unicode_is_uppercase(robo_uchar32_t cp)
{
    return robo_ucd_is_uppercase(cp);
}

int robo_unicode_is_lowercase(robo_uchar32_t cp)
{
    return robo_ucd_is_lowercase(cp);
}

int robo_unicode_is_default_ignorable(robo_uchar32_t cp)
{
    return robo_ucd_is_default_ignorable_code_point(cp);
}

int robo_unicode_is_emoji(robo_uchar32_t cp)
{
    return robo_ucd_is_emoji(cp);
}

int robo_unicode_is_emoji_presentation(robo_uchar32_t cp)
{
    return robo_ucd_is_emoji_presentation(cp);
}

int robo_unicode_is_extended_pictographic(robo_uchar32_t cp)
{
    return robo_ucd_is_extended_pictographic(cp);
}

const char *robo_unicode_category_name(robo_unicode_general_category_t gc)
{
    return robo_ucd_general_category_name(gc);
}

const char *robo_unicode_script_name(robo_unicode_script_t sc)
{
    return robo_ucd_script_name(sc);
}

const char *robo_unicode_ucd_version_string(void)
{
    return robo_ucd_version_string();
}
