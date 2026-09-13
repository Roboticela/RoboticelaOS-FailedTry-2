/*
 * RoboUnicodeProps — Unicode Character Database property queries (Goal 3.4).
 *
 * Backed by generated tables from Unicode 16.0.0 UCD (see Tools/ucd/).
 * Encoding APIs remain in RoboUnicode.h; property enums/tables in RoboUcdData.h.
 */

#ifndef ROBO_UNICODE_PROPS_H
#define ROBO_UNICODE_PROPS_H

#include "RoboUnicode/RoboUcdData.h"
#include "RoboUnicode/RoboUnicode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Same pin as encoding + UCD tables. */
#define ROBO_UNICODE_UCD_VERSION_STRING ROBO_UCD_VERSION_STRING

robo_unicode_general_category_t robo_unicode_category(robo_uchar32_t cp);
robo_unicode_script_t robo_unicode_script(robo_uchar32_t cp);
unsigned robo_unicode_combining_class(robo_uchar32_t cp);
robo_unicode_east_asian_width_t robo_unicode_east_asian_width(robo_uchar32_t cp);

int robo_unicode_is_whitespace(robo_uchar32_t cp);
int robo_unicode_is_alphabetic(robo_uchar32_t cp);
int robo_unicode_is_uppercase(robo_uchar32_t cp);
int robo_unicode_is_lowercase(robo_uchar32_t cp);
int robo_unicode_is_default_ignorable(robo_uchar32_t cp);

int robo_unicode_is_emoji(robo_uchar32_t cp);
int robo_unicode_is_emoji_presentation(robo_uchar32_t cp);
int robo_unicode_is_extended_pictographic(robo_uchar32_t cp);

const char *robo_unicode_category_name(robo_unicode_general_category_t gc);
const char *robo_unicode_script_name(robo_unicode_script_t sc);
const char *robo_unicode_ucd_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_PROPS_H */
