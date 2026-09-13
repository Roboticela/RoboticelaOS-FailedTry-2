/*
 * RoboI18N runtime locale service (Goal 4.16).
 */

#include "RoboI18N/RoboI18NLocaleService.h"

#include "RoboI18N/RoboCollData.h"
#include "RoboI18N/RoboDispData.h"
#include "RoboI18N/RoboHcData.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NLocaleNegotiate.h"
#include "RoboI18N/RoboI18NVersion.h"

#include <string.h>

static robo_i18n_locale_t g_application_locale;
static int g_application_locale_set;

void robo_i18n_locale_overrides_init(robo_i18n_locale_overrides_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

void robo_i18n_unicode_keywords_clear(robo_i18n_unicode_keywords_t *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}

void robo_i18n_locale_settings_clear(robo_i18n_locale_settings_t *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}

static void to_lower_copy(const char *in, size_t n, char *out, size_t cap)
{
    size_t i;
    if (!out || cap == 0) {
        return;
    }
    if (n >= cap) {
        n = cap - 1u;
    }
    for (i = 0; i < n; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        out[i] = c;
    }
    out[n] = '\0';
}

static int key_eq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void store_known(robo_i18n_unicode_keywords_t *kw, const char *key, const char *type)
{
    if (key_eq(key, "ca")) {
        strncpy(kw->calendar, type, sizeof(kw->calendar) - 1u);
    } else if (key_eq(key, "nu")) {
        strncpy(kw->numbering_system, type, sizeof(kw->numbering_system) - 1u);
    } else if (key_eq(key, "hc")) {
        strncpy(kw->hour_cycle, type, sizeof(kw->hour_cycle) - 1u);
    } else if (key_eq(key, "co")) {
        strncpy(kw->collation, type, sizeof(kw->collation) - 1u);
    } else if (key_eq(key, "tz")) {
        strncpy(kw->timezone, type, sizeof(kw->timezone) - 1u);
    } else {
        size_t n = strlen(kw->other);
        size_t kn = strlen(key);
        size_t tn = strlen(type);
        if (n + kn + tn + 3u < sizeof(kw->other)) {
            if (n > 0) {
                kw->other[n++] = '-';
            }
            memcpy(kw->other + n, key, kn);
            n += kn;
            kw->other[n++] = '-';
            memcpy(kw->other + n, type, tn);
            n += tn;
            kw->other[n] = '\0';
        }
    }
}

robo_result_t robo_i18n_locale_unicode_keywords(const robo_i18n_locale_t *loc,
                                                robo_i18n_unicode_keywords_t *out)
{
    const char *p;
    char key[ROBO_I18N_UKEY_MAX + 1u];
    char type[ROBO_I18N_UTYPE_MAX + 1u];
    size_t n;

    if (!loc || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_i18n_unicode_keywords_clear(out);
    p = loc->extensions;
    if (!p[0]) {
        return ROBO_OK;
    }
    /* Expect leading "u-" */
    if (!(p[0] == 'u' || p[0] == 'U') || p[1] != '-') {
        return ROBO_OK; /* non-u extension: ignore for keywords */
    }
    p += 2;
    while (*p) {
        /* attribute (3–8) or key (2) */
        n = 0;
        while (p[n] && p[n] != '-' && n < ROBO_I18N_UTYPE_MAX) {
            n++;
        }
        if (n == 0) {
            return ROBO_PARSE_ERROR;
        }
        if (n == 2) {
            to_lower_copy(p, 2, key, sizeof(key));
            p += 2;
            if (*p != '-') {
                return ROBO_PARSE_ERROR;
            }
            p++;
            n = 0;
            while (p[n] && p[n] != '-' && n < ROBO_I18N_UTYPE_MAX) {
                n++;
            }
            if (n < 3 || n > 8) {
                return ROBO_PARSE_ERROR;
            }
            to_lower_copy(p, n, type, sizeof(type));
            p += n;
            store_known(out, key, type);
            if (*p == '-') {
                p++;
            }
        } else {
            /* Unicode extension attribute — preserve in other */
            to_lower_copy(p, n, type, sizeof(type));
            p += n;
            {
                size_t on = strlen(out->other);
                if (on + n + 2u < sizeof(out->other)) {
                    if (on) {
                        out->other[on++] = '-';
                    }
                    memcpy(out->other + on, type, n);
                    on += n;
                    out->other[on] = '\0';
                }
            }
            if (*p == '-') {
                p++;
            }
        }
    }
    return ROBO_OK;
}

static void append_dash(char *buf, size_t cap, size_t *n)
{
    if (*n + 1u < cap) {
        buf[(*n)++] = '-';
        buf[*n] = '\0';
    }
}

static void append_str(char *buf, size_t cap, size_t *n, const char *s)
{
    size_t sn;
    if (!s || !s[0]) {
        return;
    }
    sn = strlen(s);
    if (*n + sn >= cap) {
        return;
    }
    memcpy(buf + *n, s, sn);
    *n += sn;
    buf[*n] = '\0';
}

robo_result_t robo_i18n_locale_set_unicode_keywords(robo_i18n_locale_t *loc,
                                                    const robo_i18n_unicode_keywords_t *kw)
{
    char buf[ROBO_I18N_EXT_MAX + 1u];
    size_t n = 0;
    if (!loc || !kw) {
        return ROBO_INVALID_ARGUMENT;
    }
    buf[0] = '\0';
    append_str(buf, sizeof(buf), &n, "u");
    if (kw->other[0]) {
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, kw->other);
    }
    if (kw->calendar[0]) {
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, "ca");
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, kw->calendar);
    }
    if (kw->numbering_system[0]) {
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, "nu");
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, kw->numbering_system);
    }
    if (kw->hour_cycle[0]) {
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, "hc");
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, kw->hour_cycle);
    }
    if (kw->collation[0]) {
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, "co");
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, kw->collation);
    }
    if (kw->timezone[0]) {
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, "tz");
        append_dash(buf, sizeof(buf), &n);
        append_str(buf, sizeof(buf), &n, kw->timezone);
    }
    if (n <= 1) {
        loc->extensions[0] = '\0';
        return ROBO_OK;
    }
    if (n >= sizeof(loc->extensions)) {
        return ROBO_ERROR;
    }
    memcpy(loc->extensions, buf, n + 1u);
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_get_keyword(const robo_i18n_locale_t *loc,
                                           const char *key,
                                           char *out,
                                           size_t out_cap)
{
    robo_i18n_unicode_keywords_t kw;
    const char *val = "";
    robo_result_t r;
    size_t n;
    if (!loc || !key || !out || out_cap == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_locale_unicode_keywords(loc, &kw);
    if (r != ROBO_OK) {
        return r;
    }
    if (key_eq(key, "ca")) {
        val = kw.calendar;
    } else if (key_eq(key, "nu")) {
        val = kw.numbering_system;
    } else if (key_eq(key, "hc")) {
        val = kw.hour_cycle;
    } else if (key_eq(key, "co")) {
        val = kw.collation;
    } else if (key_eq(key, "tz")) {
        val = kw.timezone;
    } else {
        return ROBO_NOT_FOUND;
    }
    n = strlen(val);
    if (n + 1u > out_cap) {
        return ROBO_ERROR;
    }
    memcpy(out, val, n + 1u);
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_set_keyword(robo_i18n_locale_t *loc,
                                           const char *key,
                                           const char *type)
{
    robo_i18n_unicode_keywords_t kw;
    robo_result_t r;
    if (!loc || !key) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_locale_unicode_keywords(loc, &kw);
    if (r != ROBO_OK) {
        return r;
    }
    if (!type) {
        type = "";
    }
    if (key_eq(key, "ca")) {
        strncpy(kw.calendar, type, sizeof(kw.calendar) - 1u);
        kw.calendar[sizeof(kw.calendar) - 1u] = '\0';
    } else if (key_eq(key, "nu")) {
        strncpy(kw.numbering_system, type, sizeof(kw.numbering_system) - 1u);
        kw.numbering_system[sizeof(kw.numbering_system) - 1u] = '\0';
    } else if (key_eq(key, "hc")) {
        strncpy(kw.hour_cycle, type, sizeof(kw.hour_cycle) - 1u);
        kw.hour_cycle[sizeof(kw.hour_cycle) - 1u] = '\0';
    } else if (key_eq(key, "co")) {
        strncpy(kw.collation, type, sizeof(kw.collation) - 1u);
        kw.collation[sizeof(kw.collation) - 1u] = '\0';
    } else if (key_eq(key, "tz")) {
        strncpy(kw.timezone, type, sizeof(kw.timezone) - 1u);
        kw.timezone[sizeof(kw.timezone) - 1u] = '\0';
    } else {
        return ROBO_NOT_SUPPORTED;
    }
    return robo_i18n_locale_set_unicode_keywords(loc, &kw);
}

static const char *lookup_hc_for_region(const char *region)
{
    uint32_t i;
    if (!region || !region[0]) {
        region = "001";
    }
    for (i = 0; i < robo_hc_region_count; i++) {
        if (strcmp(robo_hc_regions[i].region, region) == 0) {
            return robo_hc_regions[i].hour_cycle;
        }
    }
    for (i = 0; i < robo_hc_region_count; i++) {
        if (strcmp(robo_hc_regions[i].region, "001") == 0) {
            return robo_hc_regions[i].hour_cycle;
        }
    }
    return "h23";
}

robo_i18n_hour_cycle_t robo_i18n_hour_cycle_from_id(const char *id)
{
    if (!id) {
        return ROBO_I18N_HC_UNKNOWN;
    }
    if (strcmp(id, "h11") == 0) {
        return ROBO_I18N_HC_H11;
    }
    if (strcmp(id, "h12") == 0) {
        return ROBO_I18N_HC_H12;
    }
    if (strcmp(id, "h23") == 0) {
        return ROBO_I18N_HC_H23;
    }
    if (strcmp(id, "h24") == 0) {
        return ROBO_I18N_HC_H24;
    }
    return ROBO_I18N_HC_UNKNOWN;
}

const char *robo_i18n_hour_cycle_id(robo_i18n_hour_cycle_t hc)
{
    switch (hc) {
    case ROBO_I18N_HC_H11:
        return "h11";
    case ROBO_I18N_HC_H12:
        return "h12";
    case ROBO_I18N_HC_H23:
        return "h23";
    case ROBO_I18N_HC_H24:
        return "h24";
    default:
        return "";
    }
}

static void copy_field(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1u);
    dst[cap - 1u] = '\0';
}

robo_result_t robo_i18n_locale_resolve(const robo_i18n_locale_t *locale,
                                       const robo_i18n_locale_overrides_t *overrides,
                                       robo_i18n_locale_settings_t *out)
{
    robo_i18n_unicode_keywords_t kw;
    robo_i18n_locale_t maximized;
    char region[8];
    robo_result_t r;

    if (!locale || !out || locale->language[0] == '\0') {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_i18n_locale_settings_clear(out);
    out->locale = *locale;
    r = robo_i18n_locale_unicode_keywords(locale, &kw);
    if (r != ROBO_OK) {
        return r;
    }

    /* Calendar */
    if (overrides && overrides->calendar && overrides->calendar[0]) {
        copy_field(out->calendar, sizeof(out->calendar), overrides->calendar);
        out->calendar_src = ROBO_I18N_SRC_OVERRIDE;
    } else if (kw.calendar[0]) {
        copy_field(out->calendar, sizeof(out->calendar), kw.calendar);
        out->calendar_src = ROBO_I18N_SRC_EXTENSION;
    } else if (robo_i18n_locale_default_calendar(locale, out->calendar, sizeof(out->calendar)) ==
               ROBO_OK) {
        out->calendar_src = ROBO_I18N_SRC_LOCALE_DEFAULT;
    } else {
        copy_field(out->calendar, sizeof(out->calendar), "gregorian");
        out->calendar_src = ROBO_I18N_SRC_RUNTIME_FALLBACK;
    }

    /* Numbering */
    if (overrides && overrides->numbering_system && overrides->numbering_system[0]) {
        copy_field(out->numbering_system, sizeof(out->numbering_system),
                   overrides->numbering_system);
        out->numbering_src = ROBO_I18N_SRC_OVERRIDE;
    } else if (kw.numbering_system[0]) {
        copy_field(out->numbering_system, sizeof(out->numbering_system), kw.numbering_system);
        out->numbering_src = ROBO_I18N_SRC_EXTENSION;
    } else if (robo_i18n_locale_default_numbering_system(locale, out->numbering_system,
                                                         sizeof(out->numbering_system)) ==
               ROBO_OK) {
        out->numbering_src = ROBO_I18N_SRC_LOCALE_DEFAULT;
    } else {
        copy_field(out->numbering_system, sizeof(out->numbering_system), "latn");
        out->numbering_src = ROBO_I18N_SRC_RUNTIME_FALLBACK;
    }

    /* Hour cycle */
    if (overrides && overrides->hour_cycle && overrides->hour_cycle[0]) {
        copy_field(out->hour_cycle, sizeof(out->hour_cycle), overrides->hour_cycle);
        out->hour_cycle_src = ROBO_I18N_SRC_OVERRIDE;
    } else if (kw.hour_cycle[0]) {
        copy_field(out->hour_cycle, sizeof(out->hour_cycle), kw.hour_cycle);
        out->hour_cycle_src = ROBO_I18N_SRC_EXTENSION;
    } else {
        region[0] = '\0';
        if (robo_i18n_locale_maximize(locale, &maximized) == ROBO_OK && maximized.region[0]) {
            copy_field(region, sizeof(region), maximized.region);
        } else if (locale->region[0]) {
            copy_field(region, sizeof(region), locale->region);
        }
        copy_field(out->hour_cycle, sizeof(out->hour_cycle), lookup_hc_for_region(region));
        out->hour_cycle_src = ROBO_I18N_SRC_LOCALE_DEFAULT;
    }

    /* Collation type */
    if (overrides && overrides->collation_type && overrides->collation_type[0]) {
        copy_field(out->collation_type, sizeof(out->collation_type), overrides->collation_type);
        out->collation_src = ROBO_I18N_SRC_OVERRIDE;
    } else if (kw.collation[0]) {
        copy_field(out->collation_type, sizeof(out->collation_type), kw.collation);
        out->collation_src = ROBO_I18N_SRC_EXTENSION;
    } else {
        copy_field(out->collation_type, sizeof(out->collation_type), "standard");
        out->collation_src = ROBO_I18N_SRC_RUNTIME_FALLBACK;
    }

    /* Timezone — never inferred from language alone */
    if (overrides && overrides->timezone_id && overrides->timezone_id[0]) {
        copy_field(out->timezone_id, sizeof(out->timezone_id), overrides->timezone_id);
        out->timezone_src = ROBO_I18N_SRC_OVERRIDE;
    } else if (kw.timezone[0]) {
        copy_field(out->timezone_id, sizeof(out->timezone_id), kw.timezone);
        out->timezone_src = ROBO_I18N_SRC_EXTENSION;
    } else {
        out->timezone_id[0] = '\0';
        out->timezone_src = ROBO_I18N_SRC_UNSET;
    }

    return ROBO_OK;
}

robo_result_t robo_i18n_locale_resolve_numbering(const robo_i18n_locale_t *locale,
                                                 const char *override_id,
                                                 char *out_id,
                                                 size_t out_cap)
{
    robo_i18n_locale_overrides_t ov;
    robo_i18n_locale_settings_t st;
    robo_result_t r;
    robo_i18n_locale_overrides_init(&ov);
    ov.numbering_system = override_id;
    r = robo_i18n_locale_resolve(locale, &ov, &st);
    if (r != ROBO_OK) {
        return r;
    }
    if (strlen(st.numbering_system) + 1u > out_cap) {
        return ROBO_ERROR;
    }
    memcpy(out_id, st.numbering_system, strlen(st.numbering_system) + 1u);
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_resolve_calendar(const robo_i18n_locale_t *locale,
                                                const char *override_id,
                                                char *out_id,
                                                size_t out_cap)
{
    robo_i18n_locale_overrides_t ov;
    robo_i18n_locale_settings_t st;
    robo_result_t r;
    robo_i18n_locale_overrides_init(&ov);
    ov.calendar = override_id;
    r = robo_i18n_locale_resolve(locale, &ov, &st);
    if (r != ROBO_OK) {
        return r;
    }
    if (strlen(st.calendar) + 1u > out_cap) {
        return ROBO_ERROR;
    }
    memcpy(out_id, st.calendar, strlen(st.calendar) + 1u);
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_resolve_hour_cycle(const robo_i18n_locale_t *locale,
                                                  const char *override_id,
                                                  char *out_id,
                                                  size_t out_cap)
{
    robo_i18n_locale_overrides_t ov;
    robo_i18n_locale_settings_t st;
    robo_result_t r;
    robo_i18n_locale_overrides_init(&ov);
    ov.hour_cycle = override_id;
    r = robo_i18n_locale_resolve(locale, &ov, &st);
    if (r != ROBO_OK) {
        return r;
    }
    if (strlen(st.hour_cycle) + 1u > out_cap) {
        return ROBO_ERROR;
    }
    memcpy(out_id, st.hour_cycle, strlen(st.hour_cycle) + 1u);
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_resolve_collation(const robo_i18n_locale_t *locale,
                                                 const char *override_id,
                                                 char *out_id,
                                                 size_t out_cap)
{
    robo_i18n_locale_overrides_t ov;
    robo_i18n_locale_settings_t st;
    robo_result_t r;
    robo_i18n_locale_overrides_init(&ov);
    ov.collation_type = override_id;
    r = robo_i18n_locale_resolve(locale, &ov, &st);
    if (r != ROBO_OK) {
        return r;
    }
    if (strlen(st.collation_type) + 1u > out_cap) {
        return ROBO_ERROR;
    }
    memcpy(out_id, st.collation_type, strlen(st.collation_type) + 1u);
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_resolve_timezone(const robo_i18n_locale_t *locale,
                                                const char *override_id,
                                                char *out_id,
                                                size_t out_cap)
{
    robo_i18n_locale_overrides_t ov;
    robo_i18n_locale_settings_t st;
    robo_result_t r;
    robo_i18n_locale_overrides_init(&ov);
    ov.timezone_id = override_id;
    r = robo_i18n_locale_resolve(locale, &ov, &st);
    if (r != ROBO_OK) {
        return r;
    }
    if (!st.timezone_id[0]) {
        if (out_cap < 4u) {
            return ROBO_ERROR;
        }
        memcpy(out_id, "UTC", 4);
        return ROBO_OK;
    }
    if (strlen(st.timezone_id) + 1u > out_cap) {
        return ROBO_ERROR;
    }
    memcpy(out_id, st.timezone_id, strlen(st.timezone_id) + 1u);
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_fallback_chain(const robo_i18n_locale_t *loc,
                                              robo_i18n_locale_t *out,
                                              size_t cap,
                                              size_t *out_count)
{
    robo_i18n_locale_t cur;
    size_t n = 0;
    int guard = 0;
    if (!loc || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_count = 0;
    if (cap == 0 || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    cur = *loc;
    for (;;) {
        if (n >= cap || n >= ROBO_I18N_FALLBACK_MAX) {
            break;
        }
        out[n++] = cur;
        {
            robo_i18n_locale_t parent;
            if (robo_i18n_locale_data_parent(&cur, &parent) != ROBO_OK) {
                break;
            }
            if (robo_i18n_locale_equal(&cur, &parent)) {
                break;
            }
            cur = parent;
        }
        if (++guard > (int)ROBO_I18N_FALLBACK_MAX) {
            break;
        }
    }
    *out_count = n;
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_negotiate(const robo_i18n_locale_list_t *requested,
                                         const robo_i18n_locale_t *available,
                                         size_t available_count,
                                         robo_i18n_locale_t *out_best,
                                         robo_i18n_match_t *out_match)
{
    robo_i18n_preference_list_t *prefs = NULL;
    robo_i18n_negotiate_result_t res;
    robo_result_t r;

    if (!requested || !available || available_count == 0 || !out_best) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_preference_list_from_locale_list(requested, &prefs);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_preference_list_normalize(prefs);
    if (r != ROBO_OK) {
        robo_i18n_preference_list_destroy(prefs);
        return r;
    }
    r = robo_i18n_locale_negotiate_ex(prefs, available, available_count, &res);
    robo_i18n_preference_list_destroy(prefs);
    if (r != ROBO_OK) {
        return r;
    }
    *out_best = res.selected;
    if (out_match) {
        *out_match = robo_i18n_negotiate_kind_to_match(res.kind);
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_set_application_locale(const robo_i18n_locale_t *loc)
{
    if (!loc || loc->language[0] == '\0') {
        return ROBO_INVALID_ARGUMENT;
    }
    g_application_locale = *loc;
    g_application_locale_set = 1;
    return ROBO_OK;
}

robo_result_t robo_i18n_get_application_locale(robo_i18n_locale_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_application_locale_set) {
        return ROBO_NOT_FOUND;
    }
    *out = g_application_locale;
    return ROBO_OK;
}

void robo_i18n_clear_application_locale(void)
{
    memset(&g_application_locale, 0, sizeof(g_application_locale));
    g_application_locale_set = 0;
}

robo_result_t robo_i18n_get_effective_locale(robo_i18n_locale_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (g_application_locale_set) {
        *out = g_application_locale;
        return ROBO_OK;
    }
    return robo_i18n_get_default_locale(out);
}

int robo_i18n_locale_supports_calendar(const robo_i18n_locale_t *loc, const char *calendar_id)
{
    robo_i18n_calendar_t cal;
    (void)loc;
    if (!calendar_id) {
        return 0;
    }
    return robo_i18n_calendar_from_id(calendar_id, strlen(calendar_id), &cal) == ROBO_OK;
}

int robo_i18n_locale_supports_numbering(const robo_i18n_locale_t *loc, const char *ns_id)
{
    robo_i18n_numbering_system_t ns;
    (void)loc;
    if (!ns_id) {
        return 0;
    }
    return robo_i18n_numbering_system_get(ns_id, &ns) == ROBO_OK;
}

int robo_i18n_locale_supports_collation_type(const robo_i18n_locale_t *loc, const char *type)
{
    const char *lang;
    uint32_t i;
    uint16_t t;
    if (!loc || !type || !type[0]) {
        return 0;
    }
    if (strcmp(type, "standard") == 0 || strcmp(type, "search") == 0) {
        return 1; /* UCA always available */
    }
    lang = loc->language[0] ? loc->language : "root";
    for (i = 0; i < robo_coll_locale_count; i++) {
        const robo_coll_locale_t *row = &robo_coll_locales[i];
        const char *id = robo_coll_string_pool + row->id_offset;
        if (strcmp(id, lang) != 0 && strcmp(id, "root") != 0) {
            continue;
        }
        for (t = 0; t < row->type_count; t++) {
            const robo_coll_type_t *ct = &robo_coll_types[row->type_begin + t];
            const char *n = robo_coll_string_pool + ct->name_offset;
            if (strcmp(n, type) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

int robo_i18n_locale_has_display_names(const robo_i18n_locale_t *loc)
{
    uint32_t i;
    if (!loc || !loc->language[0]) {
        return 0;
    }
    for (i = 0; i < robo_disp_language_count; i++) {
        const char *disp = robo_disp_string(robo_disp_languages[i].display_locale);
        if (disp && strcmp(disp, loc->language) == 0) {
            return 1;
        }
    }
    return strcmp(loc->language, "en") == 0;
}

int robo_i18n_locale_has_timezone_display(const robo_i18n_locale_t *loc)
{
    return robo_i18n_locale_has_display_names(loc) && robo_disp_tz_pattern_count > 0u;
}

int robo_i18n_timezone_id_is_known(const char *id, size_t len)
{
    robo_i18n_timezone_t tz;
    if (!id || len == 0) {
        return 0;
    }
    return robo_i18n_timezone_from_id(id, len, &tz) == ROBO_OK;
}

void robo_i18n_data_versions(robo_i18n_data_versions_t *out)
{
    if (!out) {
        return;
    }
    out->api_version = ROBO_I18N_VERSION_STRING;
    out->generator_version = ROBO_I18N_GENERATOR_VERSION_STRING;
    out->cldr_version = ROBO_CLDR_VERSION_STRING;
    out->unicode_version = ROBO_CLDR_UNICODE_VERSION_STRING;
    out->tzdb_version = ROBO_TZDB_VERSION_STRING_PIN;
    out->iana_file_date = ROBO_IANA_REGISTRY_FILE_DATE;
    out->uca_version = ROBO_UCA_VERSION_STRING_PIN;
    out->iso4217_source = ROBO_ISO4217_SOURCE_STRING;
}
