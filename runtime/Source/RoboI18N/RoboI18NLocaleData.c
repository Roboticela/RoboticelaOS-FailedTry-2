/*
 * CLDR locale-data lookups + likely subtags (Goal 4.2).
 */

#include "RoboI18N/RoboI18NLocaleData.h"

#include "RoboI18N/RoboCldrData.h"

#include <string.h>

static void copy_id(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void append(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(dst);
    size_t m = strlen(src);
    if (n + m + 1u > cap) {
        return;
    }
    memcpy(dst + n, src, m + 1u);
}

static robo_result_t format_tag_simple(const robo_i18n_locale_t *loc, char *buf, size_t cap)
{
    size_t n = 0;
    size_t ln;
    if (!loc || !buf || cap < 2u || loc->language[0] == '\0') {
        return ROBO_INVALID_ARGUMENT;
    }
    ln = strlen(loc->language);
    if (ln + 1u > cap) {
        return ROBO_INVALID_ARGUMENT;
    }
    memcpy(buf, loc->language, ln);
    n = ln;
    if (loc->script[0]) {
        if (n + 5u >= cap) {
            return ROBO_INVALID_ARGUMENT;
        }
        buf[n++] = '-';
        memcpy(buf + n, loc->script, 4u);
        n += 4u;
    }
    if (loc->region[0]) {
        size_t rn = strlen(loc->region);
        if (n + 1u + rn >= cap) {
            return ROBO_INVALID_ARGUMENT;
        }
        buf[n++] = '-';
        memcpy(buf + n, loc->region, rn);
        n += rn;
    }
    buf[n] = '\0';
    return ROBO_OK;
}

static int32_t find_likely(const char *key)
{
    int32_t k = robo_cldr_find_str(key);
    if (k < 0) {
        return -1;
    }
    return robo_cldr_kv_lookup(robo_cldr_likely, robo_cldr_likely_count, k);
}

robo_result_t robo_i18n_locale_maximize(const robo_i18n_locale_t *in, robo_i18n_locale_t *out)
{
    char lang[16];
    char script[8];
    char region[8];
    char key[64];
    int32_t hit = -1;
    robo_i18n_locale_t tmp;
    const char *to;

    if (!in || !out || in->language[0] == '\0') {
        return ROBO_INVALID_ARGUMENT;
    }

    copy_id(lang, sizeof(lang), in->language);
    copy_id(script, sizeof(script), in->script);
    copy_id(region, sizeof(region), in->region);

    if (script[0] && region[0]) {
        key[0] = '\0';
        append(key, sizeof(key), lang);
        append(key, sizeof(key), "-");
        append(key, sizeof(key), script);
        append(key, sizeof(key), "-");
        append(key, sizeof(key), region);
        hit = find_likely(key);
    }
    if (hit < 0 && region[0]) {
        key[0] = '\0';
        append(key, sizeof(key), lang);
        append(key, sizeof(key), "-");
        append(key, sizeof(key), region);
        hit = find_likely(key);
    }
    if (hit < 0 && script[0]) {
        key[0] = '\0';
        append(key, sizeof(key), lang);
        append(key, sizeof(key), "-");
        append(key, sizeof(key), script);
        hit = find_likely(key);
    }
    if (hit < 0) {
        hit = find_likely(lang);
    }
    if (hit < 0 && script[0] && region[0]) {
        key[0] = '\0';
        append(key, sizeof(key), "und-");
        append(key, sizeof(key), script);
        append(key, sizeof(key), "-");
        append(key, sizeof(key), region);
        hit = find_likely(key);
    }
    if (hit < 0 && region[0]) {
        key[0] = '\0';
        append(key, sizeof(key), "und-");
        append(key, sizeof(key), region);
        hit = find_likely(key);
    }
    if (hit < 0 && script[0]) {
        key[0] = '\0';
        append(key, sizeof(key), "und-");
        append(key, sizeof(key), script);
        hit = find_likely(key);
    }
    if (hit < 0) {
        hit = find_likely("und");
    }

    if (hit < 0) {
        *out = *in;
        return ROBO_OK;
    }

    to = robo_cldr_str(hit);
    if (robo_i18n_locale_parse(to, strlen(to), &tmp) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    *out = *in;
    if (!out->script[0] && tmp.script[0]) {
        memcpy(out->script, tmp.script, sizeof(out->script));
    }
    if (!out->region[0] && tmp.region[0]) {
        memcpy(out->region, tmp.region, sizeof(out->region));
    }
    if (strcmp(out->language, "und") == 0 && tmp.language[0]) {
        memcpy(out->language, tmp.language, sizeof(out->language));
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_minimize(const robo_i18n_locale_t *in, robo_i18n_locale_t *out)
{
    robo_i18n_locale_t maxed;
    robo_i18n_locale_t trial;
    robo_i18n_locale_t check;
    robo_result_t r;

    if (!in || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_locale_maximize(in, &maxed);
    if (r != ROBO_OK) {
        return r;
    }
    *out = maxed;

    if (out->region[0]) {
        trial = *out;
        trial.region[0] = '\0';
        if (robo_i18n_locale_maximize(&trial, &check) == ROBO_OK &&
            robo_i18n_locale_equal(&check, &maxed)) {
            *out = trial;
        }
    }
    if (out->script[0]) {
        trial = *out;
        trial.script[0] = '\0';
        if (robo_i18n_locale_maximize(&trial, &check) == ROBO_OK &&
            robo_i18n_locale_equal(&check, &maxed)) {
            *out = trial;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_data_parent(const robo_i18n_locale_t *in, robo_i18n_locale_t *out)
{
    char tag[ROBO_I18N_TAG_MAX];
    int32_t key;
    int32_t parent_i;
    robo_result_t r;

    if (!in || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (strcmp(in->language, "root") == 0) {
        return ROBO_NOT_FOUND;
    }

    r = format_tag_simple(in, tag, sizeof(tag));
    if (r != ROBO_OK) {
        return r;
    }
    key = robo_cldr_find_str(tag);
    if (key >= 0) {
        parent_i = robo_cldr_kv_lookup(robo_cldr_parent, robo_cldr_parent_count, key);
        if (parent_i >= 0) {
            const char *p = robo_cldr_str(parent_i);
            if (strcmp(p, "root") == 0) {
                robo_i18n_locale_clear(out);
                memcpy(out->language, "root", 5);
                return ROBO_OK;
            }
            return robo_i18n_locale_parse(p, strlen(p), out);
        }
    }

    {
        robo_i18n_locale_t bare = *in;
        bare.extensions[0] = '\0';
        bare.privateuse[0] = '\0';
        bare.variant_count = 0;
        r = robo_i18n_locale_parent(&bare, out);
        if (r == ROBO_OK) {
            return ROBO_OK;
        }
    }

    robo_i18n_locale_clear(out);
    memcpy(out->language, "root", 5);
    return ROBO_OK;
}

static robo_result_t lookup_loc_num(const robo_i18n_locale_t *loc,
                                    int want_native,
                                    char *out_id,
                                    size_t out_cap)
{
    robo_i18n_locale_t cur;
    int guard = 0;

    if (!loc || !out_id || out_cap == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    cur = *loc;
    cur.extensions[0] = '\0';
    cur.privateuse[0] = '\0';
    cur.variant_count = 0;

    while (guard++ < 16) {
        char tag[ROBO_I18N_TAG_MAX];
        int32_t key;
        const robo_cldr_loc_num_t *row;
        robo_i18n_locale_t parent;

        if (strcmp(cur.language, "root") == 0) {
            copy_id(out_id, out_cap, "latn");
            return ROBO_OK;
        }
        if (format_tag_simple(&cur, tag, sizeof(tag)) != ROBO_OK) {
            return ROBO_INVALID_ARGUMENT;
        }
        key = robo_cldr_find_str(tag);
        if (key >= 0) {
            row = robo_cldr_loc_num_find(key);
            if (row) {
                int32_t idx = want_native ? row->native_ns : row->default_ns;
                if (want_native && idx < 0) {
                    idx = row->default_ns;
                }
                copy_id(out_id, out_cap, robo_cldr_str(idx));
                return ROBO_OK;
            }
        }
        if (robo_i18n_locale_data_parent(&cur, &parent) != ROBO_OK) {
            break;
        }
        cur = parent;
    }
    copy_id(out_id, out_cap, "latn");
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_default_numbering_system(const robo_i18n_locale_t *loc,
                                                        char *out_id,
                                                        size_t out_cap)
{
    return lookup_loc_num(loc, 0, out_id, out_cap);
}

robo_result_t robo_i18n_locale_native_numbering_system(const robo_i18n_locale_t *loc,
                                                       char *out_id,
                                                       size_t out_cap)
{
    return lookup_loc_num(loc, 1, out_id, out_cap);
}

robo_result_t robo_i18n_numbering_system_get(const char *id,
                                             robo_i18n_numbering_system_t *out)
{
    int32_t key;
    const robo_cldr_nsys_t *row;

    if (!id || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    key = robo_cldr_find_str(id);
    if (key < 0) {
        return ROBO_NOT_FOUND;
    }
    row = robo_cldr_nsys_find(key);
    if (!row) {
        return ROBO_NOT_FOUND;
    }
    copy_id(out->id, sizeof(out->id), robo_cldr_str(row->id));
    copy_id(out->type, sizeof(out->type), robo_cldr_str(row->type));
    copy_id(out->digits, sizeof(out->digits), robo_cldr_str(row->digits));
    return ROBO_OK;
}

static const robo_cldr_calpref_t *calpref_for_locale(const robo_i18n_locale_t *loc)
{
    char region[8];
    int32_t key;
    const robo_cldr_calpref_t *row;

    if (loc->region[0]) {
        copy_id(region, sizeof(region), loc->region);
    } else {
        robo_i18n_locale_t maxed;
        if (robo_i18n_locale_maximize(loc, &maxed) == ROBO_OK && maxed.region[0]) {
            copy_id(region, sizeof(region), maxed.region);
        } else {
            copy_id(region, sizeof(region), "001");
        }
    }
    key = robo_cldr_find_str(region);
    if (key >= 0) {
        row = robo_cldr_calpref_find(key);
        if (row) {
            return row;
        }
    }
    key = robo_cldr_find_str("001");
    if (key < 0) {
        return NULL;
    }
    return robo_cldr_calpref_find(key);
}

robo_result_t robo_i18n_locale_calendars(const robo_i18n_locale_t *loc,
                                         char out_ids[][32],
                                         size_t cap,
                                         size_t *out_count)
{
    const robo_cldr_calpref_t *row;
    size_t i;
    size_t n;

    if (!loc || !out_ids || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    row = calpref_for_locale(loc);
    if (!row) {
        *out_count = 0;
        return ROBO_NOT_FOUND;
    }
    n = row->count;
    if (n > cap) {
        n = cap;
    }
    for (i = 0; i < n; i++) {
        copy_id(out_ids[i], 32, robo_cldr_str(row->calendars[i]));
    }
    *out_count = n;
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_default_calendar(const robo_i18n_locale_t *loc,
                                                char *out_id,
                                                size_t out_cap)
{
    char ids[1][32];
    size_t n = 0;
    robo_result_t r;

    if (!out_id || out_cap == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_locale_calendars(loc, ids, 1, &n);
    if (r != ROBO_OK || n == 0) {
        copy_id(out_id, out_cap, "gregorian");
        return ROBO_OK;
    }
    copy_id(out_id, out_cap, ids[0]);
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_measurement_system(const robo_i18n_locale_t *loc,
                                                  robo_i18n_measurement_t *out)
{
    char region[8];
    int32_t key;
    int32_t val;

    if (!loc || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (loc->region[0]) {
        copy_id(region, sizeof(region), loc->region);
    } else {
        robo_i18n_locale_t maxed;
        if (robo_i18n_locale_maximize(loc, &maxed) == ROBO_OK && maxed.region[0]) {
            copy_id(region, sizeof(region), maxed.region);
        } else {
            copy_id(region, sizeof(region), "001");
        }
    }
    key = robo_cldr_find_str(region);
    val = -1;
    if (key >= 0) {
        val = robo_cldr_kv_lookup(robo_cldr_measure, robo_cldr_measure_count, key);
    }
    if (val < 0) {
        key = robo_cldr_find_str("001");
        val = robo_cldr_kv_lookup(robo_cldr_measure, robo_cldr_measure_count, key);
    }
    if (val < 0) {
        *out = ROBO_I18N_MEASURE_METRIC;
        return ROBO_OK;
    }
    *out = (robo_i18n_measurement_t)val;
    return ROBO_OK;
}

const char *robo_i18n_cldr_unicode_version_string(void)
{
    return robo_cldr_unicode_version_string();
}
