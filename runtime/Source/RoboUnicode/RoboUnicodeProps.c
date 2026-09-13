/*
 * RoboUnicodeProps — thin public wrappers over generated UCD tables.
 * Goal 3.12: Script / Script_Extensions / script-run foundation.
 */

#include "RoboUnicode/RoboUnicodeProps.h"

#include "RoboBase/RoboError.h"

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

const char *robo_unicode_script_short_name(robo_unicode_script_t sc)
{
    return robo_ucd_script_short_name(sc);
}

robo_unicode_script_t robo_unicode_script_from_name(const char *name)
{
    return robo_ucd_script_from_name(name);
}

size_t robo_unicode_script_extensions(robo_uchar32_t cp,
                                      robo_unicode_script_t *out,
                                      size_t capacity)
{
    return robo_ucd_script_extensions(cp, out, capacity);
}

int robo_unicode_has_script(robo_uchar32_t cp, robo_unicode_script_t script)
{
    return robo_ucd_has_script(cp, script);
}

const char *robo_unicode_ucd_version_string(void)
{
    return robo_ucd_version_string();
}

/*
 * Script-run foundation (shaping-prep heuristic — NOT UCD property truth).
 *
 * Policy:
 *   - Concrete scripts (not Common/Inherited/Unknown) start or continue a run
 *     of the same concrete script.
 *   - Common and Inherited continue the previous concrete script when one
 *     exists; otherwise they form/continue a Common run.
 *   - Unknown breaks to its own run.
 *
 * Callers that need raw property values must use robo_unicode_script /
 * script_extensions instead.
 */
robo_result_t robo_unicode_script_runs(const robo_uchar32_t *cps,
                                       size_t len,
                                       robo_unicode_script_run_t *out,
                                       size_t capacity,
                                       size_t *out_count)
{
    size_t i;
    size_t nruns = 0;
    robo_unicode_script_t cur = ROBO_U_SCRIPT_UNKNOWN;
    size_t run_start = 0;
    int have_run = 0;

    if (out_count) {
        *out_count = 0;
    }
    if ((!cps && len > 0u) || (capacity > 0u && !out) || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (len == 0u) {
        return ROBO_OK;
    }

    for (i = 0; i < len; i++) {
        robo_unicode_script_t sc = robo_unicode_script(cps[i]);
        robo_unicode_script_t resolved;

        if (sc == ROBO_U_SCRIPT_UNKNOWN) {
            resolved = ROBO_U_SCRIPT_UNKNOWN;
        } else if (sc == ROBO_U_SCRIPT_COMMON || sc == ROBO_U_SCRIPT_INHERITED) {
            if (have_run && cur != ROBO_U_SCRIPT_UNKNOWN &&
                cur != ROBO_U_SCRIPT_COMMON) {
                resolved = cur;
            } else {
                resolved = ROBO_U_SCRIPT_COMMON;
            }
        } else {
            resolved = sc;
        }

        if (!have_run) {
            cur = resolved;
            run_start = i;
            have_run = 1;
            continue;
        }
        if (resolved == cur) {
            continue;
        }

        /* Close previous run */
        if (nruns < capacity) {
            out[nruns].start = run_start;
            out[nruns].end = i;
            out[nruns].script = cur;
        }
        nruns++;
        cur = resolved;
        run_start = i;
    }

    if (have_run) {
        if (nruns < capacity) {
            out[nruns].start = run_start;
            out[nruns].end = len;
            out[nruns].script = cur;
        }
        nruns++;
    }

    *out_count = nruns;
    /* If nruns > capacity, out holds only the first `capacity` runs;
     * caller must enlarge and retry. */
    return ROBO_OK;
}
