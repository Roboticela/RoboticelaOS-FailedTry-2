/*
 * RoboUnicodeProps — Unicode Character Database property queries (Goals 3.4, 3.12).
 *
 * Backed by generated tables from Unicode 16.0.0 UCD (see Tools/ucd/).
 * Encoding APIs remain in RoboUnicode.h; property enums/tables in RoboUcdData.h.
 *
 * Script vs Script_Extensions are distinct (UAX #24). Common/Inherited are
 * UCD values — script-run heuristics are separate (see script_runs).
 */

#ifndef ROBO_UNICODE_PROPS_H
#define ROBO_UNICODE_PROPS_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboUcdData.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>

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
int robo_unicode_is_emoji_modifier(robo_uchar32_t cp);
int robo_unicode_is_emoji_modifier_base(robo_uchar32_t cp);
int robo_unicode_is_emoji_component(robo_uchar32_t cp);
int robo_unicode_is_extended_pictographic(robo_uchar32_t cp);
int robo_unicode_is_regional_indicator(robo_uchar32_t cp);
int robo_unicode_is_variation_selector(robo_uchar32_t cp);

const char *robo_unicode_category_name(robo_unicode_general_category_t gc);
const char *robo_unicode_script_name(robo_unicode_script_t sc);
/** ISO 15924 four-letter code (Latn, Arab, Zyyy, Zinh, Zzzz, …). */
const char *robo_unicode_script_short_name(robo_unicode_script_t sc);
/** Long or short name / Qaai → script; UNKNOWN if unrecognized. */
robo_unicode_script_t robo_unicode_script_from_name(const char *name);

/**
 * Script_Extensions (UAX #24). Always returns >= 1 for in-range code points.
 * If no explicit SCX, writes the single Script property value.
 * Writes min(count, capacity) into @p out when out != NULL.
 */
size_t robo_unicode_script_extensions(robo_uchar32_t cp,
                                      robo_unicode_script_t *out,
                                      size_t capacity);

/** 1 if SCX (or Script when no SCX) contains @p script. */
int robo_unicode_has_script(robo_uchar32_t cp, robo_unicode_script_t script);

/**
 * Minimal script-run segmentation for future shaping (heuristic, not UCD).
 * See RoboUnicodeProps.c for the Common/Inherited continuation policy.
 */
typedef struct robo_unicode_script_run {
    size_t start; /* inclusive code-point index */
    size_t end;   /* exclusive */
    robo_unicode_script_t script;
} robo_unicode_script_run_t;

robo_result_t robo_unicode_script_runs(const robo_uchar32_t *cps,
                                       size_t len,
                                       robo_unicode_script_run_t *out,
                                       size_t capacity,
                                       size_t *out_count);

const char *robo_unicode_ucd_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_PROPS_H */
