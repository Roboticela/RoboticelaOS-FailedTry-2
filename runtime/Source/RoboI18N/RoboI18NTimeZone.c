/*
 * Time-zone abstraction + IANA tzdb (Goals 4.5 + 4.14).
 */

#include "RoboI18N/RoboI18NTimeZone.h"

#include "RoboI18N/RoboTzData.h"

#include <limits.h>
#include <string.h>

const char *robo_i18n_tzdb_version_string(void)
{
    return robo_tzdb_version_string();
}

robo_result_t robo_i18n_timezone_utc(robo_i18n_timezone_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->kind = ROBO_I18N_TZ_UTC;
    out->offset_seconds = 0;
    memcpy(out->id, "UTC", 4);
    return ROBO_OK;
}

robo_result_t robo_i18n_timezone_fixed_seconds(int32_t offset_seconds,
                                               robo_i18n_timezone_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (offset_seconds < -18 * 3600 || offset_seconds > 18 * 3600) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->kind = ROBO_I18N_TZ_FIXED;
    out->offset_seconds = offset_seconds;
    {
        int32_t a = offset_seconds < 0 ? -offset_seconds : offset_seconds;
        int h = a / 3600;
        int m = (a % 3600) / 60;
        char buf[16];
        int n = 0;
        buf[n++] = offset_seconds < 0 ? '-' : '+';
        buf[n++] = (char)('0' + h / 10);
        buf[n++] = (char)('0' + h % 10);
        buf[n++] = ':';
        buf[n++] = (char)('0' + m / 10);
        buf[n++] = (char)('0' + m % 10);
        buf[n] = '\0';
        memcpy(out->id, buf, (size_t)n + 1u);
    }
    return ROBO_OK;
}

static int parse_offset(const char *s, size_t len, int32_t *out_sec)
{
    size_t i = 0;
    int sign = 1;
    int h = 0;
    int m = 0;
    int sec = 0;
    if (i < len && (s[i] == '+' || s[i] == '-')) {
        sign = s[i] == '-' ? -1 : 1;
        i++;
    } else {
        return 0;
    }
    if (i + 2 > len || s[i] < '0' || s[i] > '9' || s[i + 1] < '0' || s[i + 1] > '9') {
        return 0;
    }
    h = (s[i] - '0') * 10 + (s[i + 1] - '0');
    i += 2;
    if (i < len && s[i] == ':') {
        i++;
        if (i + 2 > len) {
            return 0;
        }
        m = (s[i] - '0') * 10 + (s[i + 1] - '0');
        i += 2;
        if (i < len && s[i] == ':') {
            i++;
            if (i + 2 > len) {
                return 0;
            }
            sec = (s[i] - '0') * 10 + (s[i + 1] - '0');
            i += 2;
        }
    } else if (i + 2 <= len && s[i] >= '0' && s[i] <= '9') {
        m = (s[i] - '0') * 10 + (s[i + 1] - '0');
        i += 2;
    }
    if (i != len || h > 18 || m > 59 || sec > 59) {
        return 0;
    }
    *out_sec = sign * (h * 3600 + m * 60 + sec);
    return 1;
}

static int eq_ci(const char *a, size_t alen, const char *b)
{
    size_t i;
    size_t blen = strlen(b);
    if (alen != blen) {
        return 0;
    }
    for (i = 0; i < alen; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

static int cmp_id(const char *a, size_t alen, const char *b)
{
    size_t blen = strlen(b);
    size_t n = alen < blen ? alen : blen;
    int c = memcmp(a, b, n);
    if (c != 0) {
        return c;
    }
    if (alen < blen) {
        return -1;
    }
    if (alen > blen) {
        return 1;
    }
    return 0;
}

static const robo_tz_zone_t *find_zone_by_id(const char *id, size_t len)
{
    uint32_t lo = 0;
    uint32_t hi = ROBO_TZ_ZONE_COUNT;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const char *zid = robo_tz_string(robo_tz_zones[mid].id);
        int c = cmp_id(id, len, zid);
        if (c == 0) {
            return &robo_tz_zones[mid];
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    return NULL;
}

static const char *resolve_alias(const char *id, size_t len)
{
    uint32_t lo = 0;
    uint32_t hi = ROBO_TZ_ALIAS_COUNT;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const char *aid = robo_tz_string(robo_tz_aliases[mid].alias_id);
        int c = cmp_id(id, len, aid);
        if (c == 0) {
            return robo_tz_string(robo_tz_aliases[mid].canonical_id);
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    return NULL;
}

static const robo_tz_zone_t *lookup_iana(const char *id, size_t len)
{
    const robo_tz_zone_t *z = find_zone_by_id(id, len);
    const char *canon;
    if (z) {
        return z;
    }
    canon = resolve_alias(id, len);
    if (!canon) {
        return NULL;
    }
    return find_zone_by_id(canon, strlen(canon));
}

robo_result_t robo_i18n_timezone_from_id(const char *id,
                                         size_t len,
                                         robo_i18n_timezone_t *out)
{
    int32_t off = 0;
    size_t i = 0;
    const robo_tz_zone_t *z;
    if (!id || !out || len == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    while (i < len && (id[i] == ' ' || id[i] == '\t')) {
        i++;
    }
    id += i;
    len -= i;
    if (eq_ci(id, len, "UTC") || eq_ci(id, len, "Z") || eq_ci(id, len, "Etc/UTC") ||
        eq_ci(id, len, "Etc/GMT")) {
        return robo_i18n_timezone_utc(out);
    }
    if (eq_ci(id, len, "local") || eq_ci(id, len, "system")) {
        memset(out, 0, sizeof(*out));
        out->kind = ROBO_I18N_TZ_SYSTEM;
        memcpy(out->id, "local", 6);
        return ROBO_OK;
    }
    if (len >= 3 && (id[0] == 'U' || id[0] == 'u') && (id[1] == 'T' || id[1] == 't') &&
        (id[2] == 'C' || id[2] == 'c') && (len == 3 || id[3] == '+' || id[3] == '-')) {
        if (len == 3) {
            return robo_i18n_timezone_utc(out);
        }
        if (parse_offset(id + 3, len - 3, &off)) {
            return robo_i18n_timezone_fixed_seconds(off, out);
        }
        return ROBO_PARSE_ERROR;
    }
    if (id[0] == '+' || id[0] == '-') {
        if (parse_offset(id, len, &off)) {
            return robo_i18n_timezone_fixed_seconds(off, out);
        }
        return ROBO_PARSE_ERROR;
    }
    if (len > ROBO_I18N_TZ_ID_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0; i < len; i++) {
        char c = id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '/' || c == '-' || c == '+')) {
            return ROBO_PARSE_ERROR;
        }
    }
    z = lookup_iana(id, len);
    if (!z) {
        return ROBO_NOT_FOUND;
    }
    memset(out, 0, sizeof(*out));
    out->kind = ROBO_I18N_TZ_IANA;
    memcpy(out->id, id, len);
    out->id[len] = '\0';
    return ROBO_OK;
}

robo_i18n_timezone_kind_t robo_i18n_timezone_kind(const robo_i18n_timezone_t *tz)
{
    return tz ? tz->kind : ROBO_I18N_TZ_UTC;
}

const char *robo_i18n_timezone_id(const robo_i18n_timezone_t *tz)
{
    return tz ? tz->id : "";
}

robo_result_t robo_i18n_timezone_canonical_id(const robo_i18n_timezone_t *tz,
                                              char *out,
                                              size_t out_cap,
                                              size_t *out_len)
{
    const char *canon;
    size_t n;
    if (!tz || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (tz->kind == ROBO_I18N_TZ_SYSTEM) {
        return ROBO_NOT_SUPPORTED;
    }
    if (tz->kind == ROBO_I18N_TZ_UTC || tz->kind == ROBO_I18N_TZ_FIXED) {
        canon = tz->id;
    } else {
        const robo_tz_zone_t *z = lookup_iana(tz->id, strlen(tz->id));
        if (!z) {
            return ROBO_NOT_FOUND;
        }
        canon = robo_tz_string(z->id);
    }
    n = strlen(canon);
    *out_len = n;
    if (!out) {
        return ROBO_OK;
    }
    if (out_cap < n + 1u) {
        *out_len = n + 1u;
        return ROBO_ERROR;
    }
    memcpy(out, canon, n + 1u);
    return ROBO_OK;
}

typedef struct tz_at {
    int32_t offset;
    int is_dst;
    const char *abbr;
} tz_at_t;

static void zone_at_sec(const robo_tz_zone_t *z, int64_t unix_sec, tz_at_t *out)
{
    uint32_t lo = z->trans_start;
    uint32_t hi = z->trans_start + z->trans_count;
    uint32_t best = (uint32_t)-1;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (robo_tz_trans[mid].unix_sec <= unix_sec) {
            best = mid;
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (best == (uint32_t)-1) {
        out->offset = z->initial_offset;
        out->is_dst = z->initial_dst;
        out->abbr = robo_tz_string(z->initial_abbr);
        return;
    }
    out->offset = robo_tz_trans[best].offset;
    out->is_dst = robo_tz_trans[best].is_dst;
    out->abbr = robo_tz_string(robo_tz_trans[best].abbr);
}

static robo_result_t iana_at(const robo_i18n_timezone_t *tz,
                             const robo_i18n_instant_t *instant,
                             tz_at_t *out)
{
    const robo_tz_zone_t *z;
    int64_t sec;
    if (!tz || !instant || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    z = lookup_iana(tz->id, strlen(tz->id));
    if (!z) {
        return ROBO_NOT_FOUND;
    }
    /* Floor-divide toward -inf for negative instants. */
    if (instant->unix_ms >= 0) {
        sec = instant->unix_ms / 1000;
    } else {
        sec = -(((-instant->unix_ms) + 999) / 1000);
    }
    zone_at_sec(z, sec, out);
    return ROBO_OK;
}

robo_result_t robo_i18n_timezone_offset_at(const robo_i18n_timezone_t *tz,
                                           const robo_i18n_instant_t *instant,
                                           int32_t *out_offset_seconds)
{
    tz_at_t at;
    robo_result_t r;
    if (!tz || !out_offset_seconds) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (tz->kind == ROBO_I18N_TZ_UTC || tz->kind == ROBO_I18N_TZ_FIXED) {
        *out_offset_seconds = tz->offset_seconds;
        return ROBO_OK;
    }
    if (tz->kind == ROBO_I18N_TZ_SYSTEM) {
        return ROBO_NOT_SUPPORTED;
    }
    r = iana_at(tz, instant, &at);
    if (r != ROBO_OK) {
        return r;
    }
    *out_offset_seconds = at.offset;
    return ROBO_OK;
}

robo_result_t robo_i18n_timezone_is_dst_at(const robo_i18n_timezone_t *tz,
                                           const robo_i18n_instant_t *instant,
                                           int *out_is_dst)
{
    tz_at_t at;
    robo_result_t r;
    if (!tz || !out_is_dst) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (tz->kind == ROBO_I18N_TZ_UTC || tz->kind == ROBO_I18N_TZ_FIXED) {
        *out_is_dst = 0;
        return ROBO_OK;
    }
    if (tz->kind == ROBO_I18N_TZ_SYSTEM) {
        return ROBO_NOT_SUPPORTED;
    }
    r = iana_at(tz, instant, &at);
    if (r != ROBO_OK) {
        return r;
    }
    *out_is_dst = at.is_dst ? 1 : 0;
    return ROBO_OK;
}

robo_result_t robo_i18n_timezone_abbreviation_at(const robo_i18n_timezone_t *tz,
                                                 const robo_i18n_instant_t *instant,
                                                 char *out,
                                                 size_t out_cap,
                                                 size_t *out_len)
{
    tz_at_t at;
    robo_result_t r;
    const char *abbr;
    size_t n;
    if (!tz || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (tz->kind == ROBO_I18N_TZ_UTC) {
        abbr = "UTC";
    } else if (tz->kind == ROBO_I18N_TZ_FIXED) {
        abbr = tz->id;
    } else if (tz->kind == ROBO_I18N_TZ_SYSTEM) {
        return ROBO_NOT_SUPPORTED;
    } else {
        r = iana_at(tz, instant, &at);
        if (r != ROBO_OK) {
            return r;
        }
        abbr = at.abbr ? at.abbr : "";
    }
    n = strlen(abbr);
    *out_len = n;
    if (!out) {
        return ROBO_OK;
    }
    if (out_cap < n + 1u) {
        *out_len = n + 1u;
        return ROBO_ERROR;
    }
    memcpy(out, abbr, n + 1u);
    return ROBO_OK;
}

robo_result_t robo_i18n_timezone_to_local(const robo_i18n_timezone_t *tz,
                                          const robo_i18n_instant_t *instant,
                                          robo_i18n_datetime_t *out_local,
                                          robo_i18n_tz_resolve_t *out_resolve)
{
    int32_t off;
    robo_i18n_instant_t shifted;
    robo_result_t r;
    if (!tz || !instant || !out_local) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_timezone_offset_at(tz, instant, &off);
    if (r != ROBO_OK) {
        return r;
    }
    if ((off > 0 && instant->unix_ms > INT64_MAX - (int64_t)off * 1000) ||
        (off < 0 && instant->unix_ms < INT64_MIN - (int64_t)off * 1000)) {
        return ROBO_ERROR;
    }
    shifted.unix_ms = instant->unix_ms + (int64_t)off * 1000;
    r = robo_i18n_instant_to_utc(&shifted, out_local);
    if (r != ROBO_OK) {
        return r;
    }
    if (out_resolve) {
        *out_resolve = ROBO_I18N_TZ_UNIQUE;
    }
    return ROBO_OK;
}

static int datetime_equal(const robo_i18n_datetime_t *a, const robo_i18n_datetime_t *b)
{
    return a->date.year == b->date.year && a->date.month == b->date.month &&
           a->date.day == b->date.day && a->time.hour == b->time.hour &&
           a->time.minute == b->time.minute && a->time.second == b->time.second;
}

robo_result_t robo_i18n_timezone_to_utc_ex(const robo_i18n_timezone_t *tz,
                                           const robo_i18n_datetime_t *local,
                                           robo_i18n_tz_disambiguation_t policy,
                                           robo_i18n_instant_t *out_instant,
                                           robo_i18n_tz_resolve_t *out_resolve)
{
    robo_i18n_instant_t as_utc;
    robo_result_t r;
    int32_t off;
    if (!tz || !local || !out_instant) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (tz->kind == ROBO_I18N_TZ_SYSTEM) {
        return ROBO_NOT_SUPPORTED;
    }
    if (tz->kind == ROBO_I18N_TZ_UTC || tz->kind == ROBO_I18N_TZ_FIXED) {
        off = tz->offset_seconds;
        r = robo_i18n_instant_from_utc(local, &as_utc);
        if (r != ROBO_OK) {
            return r;
        }
        if ((off > 0 && as_utc.unix_ms < INT64_MIN + (int64_t)off * 1000) ||
            (off < 0 && as_utc.unix_ms > INT64_MAX + (int64_t)off * 1000)) {
            return ROBO_ERROR;
        }
        out_instant->unix_ms = as_utc.unix_ms - (int64_t)off * 1000;
        if (out_resolve) {
            *out_resolve = ROBO_I18N_TZ_UNIQUE;
        }
        return ROBO_OK;
    }
    {
        const robo_tz_zone_t *z = lookup_iana(tz->id, strlen(tz->id));
        robo_i18n_instant_t candidates[4];
        int n_cand = 0;
        int64_t probe_sec;
        uint32_t i;
        int64_t offsets_try[8];
        int n_off = 0;

        if (!z) {
            return ROBO_NOT_FOUND;
        }
        r = robo_i18n_instant_from_utc(local, &as_utc);
        if (r != ROBO_OK) {
            return r;
        }
        probe_sec = as_utc.unix_ms / 1000;

        /* Collect distinct offsets from nearby transitions + initial. */
        offsets_try[n_off++] = z->initial_offset;
        for (i = 0; i < z->trans_count; i++) {
            const robo_tz_trans_t *tr = &robo_tz_trans[z->trans_start + i];
            int64_t d = tr->unix_sec - probe_sec;
            if (d < -4 * 86400LL || d > 4 * 86400LL) {
                continue;
            }
            {
                int j;
                int seen = 0;
                for (j = 0; j < n_off; j++) {
                    if (offsets_try[j] == tr->offset) {
                        seen = 1;
                        break;
                    }
                }
                if (!seen && n_off < 8) {
                    offsets_try[n_off++] = tr->offset;
                }
            }
        }
        /* Also offset immediately before/after probe via zone_at. */
        {
            tz_at_t at;
            int j;
            int seen = 0;
            zone_at_sec(z, probe_sec, &at);
            for (j = 0; j < n_off; j++) {
                if (offsets_try[j] == at.offset) {
                    seen = 1;
                    break;
                }
            }
            if (!seen && n_off < 8) {
                offsets_try[n_off++] = at.offset;
            }
            zone_at_sec(z, probe_sec - 86400, &at);
            seen = 0;
            for (j = 0; j < n_off; j++) {
                if (offsets_try[j] == at.offset) {
                    seen = 1;
                    break;
                }
            }
            if (!seen && n_off < 8) {
                offsets_try[n_off++] = at.offset;
            }
            zone_at_sec(z, probe_sec + 86400, &at);
            seen = 0;
            for (j = 0; j < n_off; j++) {
                if (offsets_try[j] == at.offset) {
                    seen = 1;
                    break;
                }
            }
            if (!seen && n_off < 8) {
                offsets_try[n_off++] = at.offset;
            }
        }

        for (i = 0; i < (uint32_t)n_off; i++) {
            int32_t o = (int32_t)offsets_try[i];
            robo_i18n_instant_t cand;
            robo_i18n_datetime_t back;
            robo_i18n_tz_resolve_t ignore;
            tz_at_t at;
            cand.unix_ms = as_utc.unix_ms - (int64_t)o * 1000;
            zone_at_sec(z, cand.unix_ms >= 0 ? cand.unix_ms / 1000
                                             : -(((-cand.unix_ms) + 999) / 1000),
                        &at);
            if (at.offset != o) {
                continue;
            }
            if (robo_i18n_timezone_to_local(tz, &cand, &back, &ignore) != ROBO_OK) {
                continue;
            }
            /* Compare wall fields ignoring nano. */
            back.time.nano = local->time.nano;
            if (!datetime_equal(&back, local)) {
                continue;
            }
            if (n_cand < 4) {
                candidates[n_cand++] = cand;
            }
        }

        if (n_cand == 0) {
            if (out_resolve) {
                *out_resolve = ROBO_I18N_TZ_NONEXISTENT;
            }
            return ROBO_INVALID_ARGUMENT;
        }
        if (n_cand == 1) {
            *out_instant = candidates[0];
            if (out_resolve) {
                *out_resolve = ROBO_I18N_TZ_UNIQUE;
            }
            return ROBO_OK;
        }
        /* Ambiguous: sort by unix_ms. */
        if (candidates[0].unix_ms > candidates[1].unix_ms) {
            robo_i18n_instant_t tmp = candidates[0];
            candidates[0] = candidates[1];
            candidates[1] = tmp;
        }
        if (out_resolve) {
            *out_resolve = ROBO_I18N_TZ_AMBIGUOUS;
        }
        if (policy == ROBO_I18N_TZ_DISAMBIG_REJECT) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (policy == ROBO_I18N_TZ_DISAMBIG_LATER) {
            *out_instant = candidates[1];
        } else {
            *out_instant = candidates[0]; /* EARLIER */
        }
        return ROBO_OK;
    }
}

robo_result_t robo_i18n_timezone_to_utc(const robo_i18n_timezone_t *tz,
                                        const robo_i18n_datetime_t *local,
                                        robo_i18n_instant_t *out_instant,
                                        robo_i18n_tz_resolve_t *out_resolve)
{
    return robo_i18n_timezone_to_utc_ex(tz, local, ROBO_I18N_TZ_DISAMBIG_EARLIER, out_instant,
                                        out_resolve);
}

static void fill_transition(const robo_tz_zone_t *z,
                            uint32_t idx,
                            robo_i18n_tz_transition_t *out)
{
    tz_at_t before;
    const robo_tz_trans_t *tr = &robo_tz_trans[idx];
    int64_t before_sec = tr->unix_sec - 1;
    memset(out, 0, sizeof(*out));
    out->at.unix_ms = tr->unix_sec * 1000;
    zone_at_sec(z, before_sec, &before);
    out->offset_before = before.offset;
    out->is_dst_before = before.is_dst;
    strncpy(out->abbr_before, before.abbr ? before.abbr : "", ROBO_I18N_TZ_ABBR_MAX - 1u);
    out->offset_after = tr->offset;
    out->is_dst_after = tr->is_dst;
    strncpy(out->abbr_after, robo_tz_string(tr->abbr), ROBO_I18N_TZ_ABBR_MAX - 1u);
}

robo_result_t robo_i18n_timezone_next_transition(const robo_i18n_timezone_t *tz,
                                                 const robo_i18n_instant_t *instant,
                                                 robo_i18n_tz_transition_t *out)
{
    const robo_tz_zone_t *z;
    int64_t sec;
    uint32_t lo;
    uint32_t hi;
    if (!tz || !instant || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (tz->kind != ROBO_I18N_TZ_IANA) {
        return ROBO_NOT_FOUND;
    }
    z = lookup_iana(tz->id, strlen(tz->id));
    if (!z || z->trans_count == 0) {
        return ROBO_NOT_FOUND;
    }
    sec = instant->unix_ms >= 0 ? instant->unix_ms / 1000
                                : -(((-instant->unix_ms) + 999) / 1000);
    lo = z->trans_start;
    hi = z->trans_start + z->trans_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (robo_tz_trans[mid].unix_sec <= sec) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo >= z->trans_start + z->trans_count) {
        return ROBO_NOT_FOUND;
    }
    fill_transition(z, lo, out);
    return ROBO_OK;
}

robo_result_t robo_i18n_timezone_prev_transition(const robo_i18n_timezone_t *tz,
                                                 const robo_i18n_instant_t *instant,
                                                 robo_i18n_tz_transition_t *out)
{
    const robo_tz_zone_t *z;
    int64_t sec;
    uint32_t lo;
    uint32_t hi;
    uint32_t best;
    if (!tz || !instant || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (tz->kind != ROBO_I18N_TZ_IANA) {
        return ROBO_NOT_FOUND;
    }
    z = lookup_iana(tz->id, strlen(tz->id));
    if (!z || z->trans_count == 0) {
        return ROBO_NOT_FOUND;
    }
    sec = instant->unix_ms >= 0 ? instant->unix_ms / 1000
                                : -(((-instant->unix_ms) + 999) / 1000);
    lo = z->trans_start;
    hi = z->trans_start + z->trans_count;
    best = (uint32_t)-1;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (robo_tz_trans[mid].unix_sec < sec) {
            best = mid;
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (best == (uint32_t)-1) {
        return ROBO_NOT_FOUND;
    }
    fill_transition(z, best, out);
    return ROBO_OK;
}
