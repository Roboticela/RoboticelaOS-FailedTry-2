/*
 * CLDR-backed currency formatting (Goal 4.21).
 */

#include "RoboI18N/RoboI18NCurrency.h"

#include "RoboI18N/RoboCurrencyData.h"
#include "RoboI18N/RoboI18NDisplayNames.h"
#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboI18NPlural.h"

#include <stdint.h>
#include <string.h>

static void copy_id(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!dst || cap == 0u) {
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

static int ascii_is_alpha(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int ascii_is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static char ascii_upper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static void format_tag_simple(const robo_i18n_locale_t *loc, char *buf, size_t cap)
{
    size_t n = 0u;
    size_t ln;
    buf[0] = '\0';
    if (!loc || !buf || cap < 2u || loc->language[0] == '\0') {
        return;
    }
    ln = strlen(loc->language);
    if (ln + 1u > cap) {
        return;
    }
    memcpy(buf, loc->language, ln);
    n = ln;
    buf[n] = '\0';
    if (loc->script[0]) {
        if (n + 5u >= cap) {
            return;
        }
        buf[n++] = '-';
        memcpy(buf + n, loc->script, 4u);
        n += 4u;
        buf[n] = '\0';
    }
    if (loc->region[0]) {
        size_t rn = strlen(loc->region);
        if (n + 1u + rn >= cap) {
            return;
        }
        buf[n++] = '-';
        memcpy(buf + n, loc->region, rn);
        n += rn;
        buf[n] = '\0';
    }
}

static int read_keyword_from_other(const robo_i18n_locale_t *loc,
                                   const char *key,
                                   char *out,
                                   size_t out_cap)
{
    robo_i18n_unicode_keywords_t kw;
    const char *p;
    char needle[16];
    size_t klen;

    if (!loc || !key || !out || out_cap == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (robo_i18n_locale_get_keyword(loc, key, out, out_cap) == ROBO_OK && out[0]) {
        return 1;
    }
    if (robo_i18n_locale_unicode_keywords(loc, &kw) != ROBO_OK) {
        return 0;
    }
    klen = strlen(key);
    if (klen + 2u >= sizeof(needle)) {
        return 0;
    }
    memcpy(needle, key, klen);
    needle[klen] = '-';
    needle[klen + 1u] = '\0';
    /* Unknown keys live in kw.other as key-type-key-type… */
    p = kw.other;
    while (p && *p) {
        const char *hit = strstr(p, needle);
        size_t i = 0u;
        if (!hit) {
            break;
        }
        if (hit == kw.other || hit[-1] == '-') {
            hit += klen + 1u;
            while (hit[i] && hit[i] != '-' && i + 1u < out_cap) {
                out[i] = hit[i];
                i++;
            }
            out[i] = '\0';
            return out[0] != '\0';
        }
        p = hit + 1;
    }
    return 0;
}

void robo_i18n_currency_options_init(robo_i18n_currency_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->style = ROBO_I18N_CURRENCY_STYLE_SYMBOL;
    opts->usage = ROBO_I18N_CURRENCY_USAGE_STANDARD;
    opts->rounding = ROBO_I18N_ROUND_HALF_EVEN;
}

robo_result_t robo_i18n_currency_normalize(const char *code,
                                           size_t code_len,
                                           char out[4])
{
    size_t i;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out[0] = '\0';
    if (!code || code_len != 3u) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < 3u; i++) {
        unsigned char c = (unsigned char)code[i];
        if (!ascii_is_alpha(c)) {
            return ROBO_INVALID_ARGUMENT;
        }
        out[i] = ascii_upper((char)c);
    }
    out[3] = '\0';
    return ROBO_OK;
}

int robo_i18n_currency_is_well_formed(const char *code, size_t code_len)
{
    char tmp[4];
    return robo_i18n_currency_normalize(code, code_len, tmp) == ROBO_OK;
}

static void fill_default_info(robo_i18n_currency_info_t *out, const char code[4])
{
    memset(out, 0, sizeof(*out));
    memcpy(out->code, code, 4u);
    out->numeric = -1;
    out->digits = 2u;
    out->rounding = 0u;
    out->cash_digits = -1;
    out->cash_rounding = -1;
    out->flags = 0u;
    out->known = 0u;
}

robo_result_t robo_i18n_currency_lookup(const char *code,
                                        size_t code_len,
                                        robo_i18n_currency_info_t *out)
{
    char norm[4];
    const robo_curr_meta_t *meta;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_currency_normalize(code, code_len, norm);
    if (r != ROBO_OK) {
        return r;
    }
    meta = robo_curr_meta_find(norm, 3u);
    if (!meta) {
        fill_default_info(out, norm);
        return ROBO_NOT_FOUND;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->code, norm, 4u);
    out->numeric = meta->numeric;
    out->digits = meta->digits;
    out->rounding = meta->rounding;
    out->cash_digits = meta->cash_digits;
    out->cash_rounding = meta->cash_rounding;
    out->flags = meta->flags;
    out->known = 1u;
    return ROBO_OK;
}

robo_result_t robo_i18n_currency_for_region(const char *region,
                                            size_t region_len,
                                            char out[4])
{
    const robo_curr_region_t *row;
    const char *code;
    char reg[8];
    size_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out[0] = '\0';
    if (!region || region_len == 0u || region_len >= sizeof(reg)) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < region_len; i++) {
        reg[i] = ascii_upper(region[i]);
    }
    reg[region_len] = '\0';
    row = robo_curr_region_find(reg, region_len);
    if (!row) {
        return ROBO_NOT_FOUND;
    }
    code = robo_curr_string(row->currency);
    if (!code || strlen(code) != 3u) {
        return ROBO_NOT_FOUND;
    }
    memcpy(out, code, 4u);
    return ROBO_OK;
}

robo_result_t robo_i18n_currency_for_locale(const robo_i18n_locale_t *locale,
                                            char out[4])
{
    char cu[16];
    robo_i18n_locale_t maxed;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out[0] = '\0';
    if (!locale) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (read_keyword_from_other(locale, "cu", cu, sizeof(cu))) {
        return robo_i18n_currency_normalize(cu, strlen(cu), out);
    }
    if (robo_i18n_locale_maximize(locale, &maxed) != ROBO_OK) {
        maxed = *locale;
    }
    if (maxed.region[0] == '\0') {
        return ROBO_NOT_FOUND;
    }
    r = robo_i18n_currency_for_region(maxed.region, strlen(maxed.region), out);
    return r;
}

static uint8_t effective_digits(const robo_i18n_currency_info_t *info,
                                robo_i18n_currency_usage_t usage)
{
    if (usage == ROBO_I18N_CURRENCY_USAGE_CASH && info->cash_digits >= 0) {
        return (uint8_t)info->cash_digits;
    }
    return info->digits;
}

static int effective_rounding_inc(const robo_i18n_currency_info_t *info,
                                  robo_i18n_currency_usage_t usage)
{
    if (usage == ROBO_I18N_CURRENCY_USAGE_CASH) {
        if (info->cash_rounding >= 0) {
            return (int)info->cash_rounding;
        }
        return (int)info->rounding;
    }
    return (int)info->rounding;
}

/**
 * Round decimal to multiple of (inc * 10^-digits) using integer arithmetic.
 * inc<=1 → only truncate/pad via later number options (no extra step).
 */
static robo_result_t apply_increment_rounding(robo_i18n_decimal_t *dec,
                                              int digits,
                                              int inc,
                                              robo_i18n_rounding_t mode)
{
    /* Represent value as integer in units of 10^-digits, then round to inc. */
    char tmp[ROBO_I18N_DECIMAL_DIGITS_MAX + 8u];
    size_t int_len;
    size_t frac_len;
    size_t i;
    size_t pos = 0u;
    int64_t units = 0;
    int64_t rounded;
    int64_t q;
    int64_t rem;
    int negative;
    int64_t half;

    if (!dec || dec->kind != ROBO_I18N_DECIMAL_NUMBER) {
        return ROBO_OK;
    }
    if (digits < 0 || digits > 18 || inc <= 1) {
        return ROBO_OK;
    }
    if (dec->digit_count == 0u) {
        return ROBO_OK;
    }

    negative = dec->negative ? 1 : 0;
    if ((size_t)dec->scale > (size_t)dec->digit_count) {
        return ROBO_ERROR;
    }
    int_len = (size_t)dec->digit_count - (size_t)dec->scale;
    frac_len = (size_t)dec->scale;

    /* Build digit string with exactly `digits` fraction places (pad/trunc raw). */
    if (int_len == 0u) {
        tmp[pos++] = '0';
    } else {
        if (int_len + (size_t)digits + 1u >= sizeof(tmp)) {
            return ROBO_NOT_SUPPORTED;
        }
        memcpy(tmp + pos, dec->digits, int_len);
        pos += int_len;
    }
    for (i = 0u; i < (size_t)digits; i++) {
        char ch = '0';
        if (i < frac_len) {
            ch = dec->digits[int_len + i];
        }
        tmp[pos++] = ch;
    }
    tmp[pos] = '\0';

    /* Parse to int64 — reject overflow. */
    for (i = 0u; i < pos; i++) {
        int digit = tmp[i] - '0';
        if (units > (INT64_MAX - digit) / 10) {
            return ROBO_NOT_SUPPORTED;
        }
        units = units * 10 + digit;
    }

    q = units / inc;
    rem = units % inc;
    half = inc / 2;
    rounded = q * inc;
    if (rem != 0) {
        int bump = 0;
        switch (mode) {
        case ROBO_I18N_ROUND_DOWN:
            bump = 0;
            break;
        case ROBO_I18N_ROUND_UP:
            bump = 1;
            break;
        case ROBO_I18N_ROUND_CEILING:
            bump = negative ? 0 : 1;
            break;
        case ROBO_I18N_ROUND_FLOOR:
            bump = negative ? 1 : 0;
            break;
        case ROBO_I18N_ROUND_HALF_DOWN:
            bump = (rem > half) ? 1 : 0;
            break;
        case ROBO_I18N_ROUND_HALF_UP:
            bump = (rem >= half) ? 1 : 0;
            break;
        case ROBO_I18N_ROUND_HALF_EVEN:
        default:
            if (rem > half) {
                bump = 1;
            } else if (rem < half) {
                bump = 0;
            } else {
                /* tie: even multiple of inc */
                bump = ((q % 2) != 0) ? 1 : 0;
            }
            break;
        }
        if (bump) {
            if (rounded > INT64_MAX - inc) {
                return ROBO_NOT_SUPPORTED;
            }
            rounded += inc;
        }
    }

    /* Write back as decimal with `digits` scale. */
    {
        char digs[32];
        size_t dn = 0u;
        int64_t v = rounded;
        if (v == 0) {
            digs[dn++] = '0';
        } else {
            char rev[32];
            size_t rn = 0u;
            while (v > 0 && rn < sizeof(rev)) {
                rev[rn++] = (char)('0' + (v % 10));
                v /= 10;
            }
            while (rn > 0u) {
                digs[dn++] = rev[--rn];
            }
        }
        /* Ensure at least digits+1 digits for scale */
        while (dn <= (size_t)digits && dn < sizeof(digs)) {
            memmove(digs + 1u, digs, dn);
            digs[0] = '0';
            dn++;
        }
        if (dn > ROBO_I18N_DECIMAL_DIGITS_MAX) {
            return ROBO_NOT_SUPPORTED;
        }
        memset(dec, 0, sizeof(*dec));
        dec->kind = ROBO_I18N_DECIMAL_NUMBER;
        dec->negative = (uint8_t)negative;
        dec->scale = (uint8_t)digits;
        dec->digit_count = (uint8_t)dn;
        memcpy(dec->digits, digs, dn);
    }
    return ROBO_OK;
}

static int symbol_is_alphanumeric(const char *sym)
{
    const unsigned char *p;
    if (!sym || !sym[0]) {
        return 0;
    }
    /* Alpha-next-to-number patterns apply when the currency text contains
     * Latin letters/digits (e.g. CHF, Rs). Pure symbols ($ € £ ¥) do not. */
    for (p = (const unsigned char *)sym; *p; p++) {
        if (ascii_is_alpha(*p) || ascii_is_digit(*p)) {
            return 1;
        }
    }
    return 0;
}

static const robo_curr_fmt_t *resolve_fmt(const robo_i18n_locale_t *locale,
                                          const char *nsys)
{
    robo_i18n_locale_t cur;
    int guard = 0;

    if (!locale || !nsys || !nsys[0]) {
        return NULL;
    }
    cur = *locale;
    cur.extensions[0] = '\0';
    cur.privateuse[0] = '\0';
    cur.variant_count = 0;

    while (guard++ < 16) {
        char tag[ROBO_I18N_TAG_MAX];
        const robo_curr_fmt_t *row;
        format_tag_simple(&cur, tag, sizeof(tag));
        if (tag[0]) {
            row = robo_curr_fmt_find(tag, strlen(tag), nsys, strlen(nsys));
            if (row) {
                return row;
            }
        }
        if (robo_i18n_locale_data_parent(&cur, &cur) != ROBO_OK) {
            break;
        }
    }
    return robo_curr_fmt_find("und", 3u, nsys, strlen(nsys));
}

static robo_result_t append_bytes(char *buf,
                                  size_t cap,
                                  size_t *pos,
                                  const char *src,
                                  size_t len,
                                  int writing)
{
    if (!pos) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (writing) {
        if (!buf) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (*pos + len + 1u > cap) {
            *pos += len;
            return ROBO_ERROR;
        }
        memcpy(buf + *pos, src, len);
    }
    *pos += len;
    return ROBO_OK;
}

static void find_number_span(const char *pat, size_t len, size_t *start, size_t *end)
{
    size_t i;
    size_t s = len;
    size_t e = len;
    for (i = 0u; i < len; i++) {
        char c = pat[i];
        if (c == '#' || c == '0') {
            s = i;
            break;
        }
    }
    if (s == len) {
        *start = 0u;
        *end = 0u;
        return;
    }
    e = s;
    for (i = s; i < len; i++) {
        char c = pat[i];
        if (c == '#' || c == '0' || c == ',' || c == '.' || c == '@' || c == 'E' ||
            c == '+') {
            e = i + 1u;
        } else {
            break;
        }
    }
    *start = s;
    *end = e;
}

static size_t count_currency_signs(const char *s, size_t len, size_t at)
{
    size_t n = 0u;
    while (at + n < len && (unsigned char)s[at + n] == 0xC2u &&
           at + n + 1u < len && (unsigned char)s[at + n + 1u] == 0xA4u) {
        /* UTF-8 ¤ is C2 A4 */
        n += 2u;
    }
    /* Also plain ASCII rarely — CLDR uses U+00A4 */
    return n / 2u;
}

static int is_currency_sign_at(const char *s, size_t len, size_t at)
{
    return at + 1u < len && (unsigned char)s[at] == 0xC2u &&
           (unsigned char)s[at + 1u] == 0xA4u;
}

static robo_result_t resolve_currency_text(const robo_i18n_currency_formatter_t *fmt,
                                           const robo_i18n_decimal_t *value,
                                           char *out,
                                           size_t out_cap,
                                           size_t *out_len)
{
    robo_i18n_currency_name_style_t st;
    robo_result_t r;

    if (!fmt || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_len = 0u;

    if (fmt->options.style == ROBO_I18N_CURRENCY_STYLE_CODE) {
        size_t n = 3u;
        if (out && out_cap > 0u) {
            if (out_cap <= n) {
                *out_len = n;
                return ROBO_ERROR;
            }
            memcpy(out, fmt->resolved_currency, 3u);
            out[3] = '\0';
        }
        *out_len = n;
        return ROBO_OK;
    }

    if (fmt->options.style == ROBO_I18N_CURRENCY_STYLE_NAME) {
        /* Plural-sensitive name when tables provide it. */
        robo_i18n_locale_t cur = fmt->locale;
        robo_i18n_plural_operands_t ops;
        robo_i18n_plural_category_t cat = ROBO_I18N_PLURAL_OTHER;
        int guard = 0;
        const char *name = NULL;

        (void)robo_i18n_plural_operands_from_decimal(value, &ops);
        (void)robo_i18n_plural_select(&fmt->locale, ROBO_I18N_PLURAL_CARDINAL, &ops,
                                      &cat);

        cur.extensions[0] = '\0';
        while (guard++ < 16) {
            char tag[ROBO_I18N_TAG_MAX];
            const robo_curr_plural_name_t *row;
            format_tag_simple(&cur, tag, sizeof(tag));
            row = robo_curr_plural_name_find(tag, strlen(tag), fmt->resolved_currency,
                                             3u);
            if (row) {
                int idx = (int)cat;
                int32_t ni = row->names[idx >= 0 && idx < 6 ? idx : 5];
                if (ni < 0) {
                    ni = row->names[5];
                }
                name = robo_curr_string(ni);
                if (name) {
                    break;
                }
            }
            if (robo_i18n_locale_data_parent(&cur, &cur) != ROBO_OK) {
                break;
            }
        }
        if (name) {
            size_t n = strlen(name);
            if (out && out_cap > 0u) {
                if (out_cap <= n) {
                    *out_len = n;
                    return ROBO_ERROR;
                }
                memcpy(out, name, n);
                out[n] = '\0';
            }
            *out_len = n;
            return ROBO_OK;
        }
        st = ROBO_I18N_CURRENCY_NAME;
    } else if (fmt->options.style == ROBO_I18N_CURRENCY_STYLE_NARROW_SYMBOL) {
        st = ROBO_I18N_CURRENCY_NARROW_SYMBOL;
    } else {
        st = ROBO_I18N_CURRENCY_SYMBOL;
    }

    r = robo_i18n_currency_display_name(fmt->resolved_currency, 3u, st, &fmt->locale,
                                        NULL, out, out_cap, out_len);
    if (r == ROBO_NOT_FOUND || (r == ROBO_OK && (!out || *out_len == 0u))) {
        /* Fall back to ISO code. */
        size_t n = 3u;
        if (out && out_cap > n) {
            memcpy(out, fmt->resolved_currency, 3u);
            out[3] = '\0';
            *out_len = n;
            return ROBO_OK;
        }
        *out_len = n;
        return out && out_cap > 0u ? ROBO_ERROR : ROBO_OK;
    }
    return r;
}

static robo_result_t expand_currency_pattern(const robo_i18n_currency_formatter_t *fmt,
                                             const char *pattern,
                                             const char *number_utf8,
                                             size_t number_len,
                                             const char *curr_utf8,
                                             size_t curr_len,
                                             int prepend_minus,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len)
{
    size_t plen;
    size_t num_start = 0u;
    size_t num_end = 0u;
    size_t i;
    size_t pos = 0u;
    int writing = (buf != NULL && buf_cap > 0u);
    robo_result_t r = ROBO_OK;
    const char *minus = fmt->number.minus_sign ? fmt->number.minus_sign : "-";

    if (!pattern || !number_utf8 || !curr_utf8 || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_len = 0u;
    plen = strlen(pattern);
    find_number_span(pattern, plen, &num_start, &num_end);

    if (prepend_minus) {
        r = append_bytes(buf, buf_cap, &pos, minus, strlen(minus), writing);
        if (r != ROBO_OK && r != ROBO_ERROR) {
            return r;
        }
    }

    i = 0u;
    while (i < plen) {
        if (i == num_start && num_end > num_start) {
            r = append_bytes(buf, buf_cap, &pos, number_utf8, number_len, writing);
            if (r != ROBO_OK && r != ROBO_ERROR) {
                return r;
            }
            i = num_end;
            continue;
        }
        if (is_currency_sign_at(pattern, plen, i)) {
            size_t signs = count_currency_signs(pattern, plen, i);
            /* 1 sign = symbol/code already resolved; 2 = ISO; 3 = name — we pass
             * pre-resolved curr_utf8 for the active style. */
            (void)signs;
            r = append_bytes(buf, buf_cap, &pos, curr_utf8, curr_len, writing);
            if (r != ROBO_OK && r != ROBO_ERROR) {
                return r;
            }
            i += 2u; /* one ¤ */
            /* Skip extra ¤¤ / ¤¤¤ — already resolved via style */
            while (is_currency_sign_at(pattern, plen, i)) {
                i += 2u;
            }
            continue;
        }
        r = append_bytes(buf, buf_cap, &pos, pattern + i, 1u, writing);
        if (r != ROBO_OK && r != ROBO_ERROR) {
            return r;
        }
        i++;
    }

    if (writing) {
        if (pos + 1u > buf_cap) {
            *out_len = pos;
            return ROBO_ERROR;
        }
        buf[pos] = '\0';
    }
    *out_len = pos;
    return (writing && pos + 1u > buf_cap) ? ROBO_ERROR : ROBO_OK;
}

static robo_result_t expand_unit_pattern(const char *pattern,
                                         const char *a,
                                         size_t a_len,
                                         const char *b,
                                         size_t b_len,
                                         char *buf,
                                         size_t buf_cap,
                                         size_t *out_len)
{
    size_t i;
    size_t plen;
    size_t pos = 0u;
    int writing = (buf != NULL && buf_cap > 0u);
    robo_result_t r;

    if (!pattern || !a || !b || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    plen = strlen(pattern);
    for (i = 0u; i < plen;) {
        if (pattern[i] == '{' && i + 2u < plen && pattern[i + 2u] == '}') {
            if (pattern[i + 1u] == '0') {
                r = append_bytes(buf, buf_cap, &pos, a, a_len, writing);
                if (r != ROBO_OK && r != ROBO_ERROR) {
                    return r;
                }
                i += 3u;
                continue;
            }
            if (pattern[i + 1u] == '1') {
                r = append_bytes(buf, buf_cap, &pos, b, b_len, writing);
                if (r != ROBO_OK && r != ROBO_ERROR) {
                    return r;
                }
                i += 3u;
                continue;
            }
        }
        r = append_bytes(buf, buf_cap, &pos, pattern + i, 1u, writing);
        if (r != ROBO_OK && r != ROBO_ERROR) {
            return r;
        }
        i++;
    }
    if (writing) {
        if (pos + 1u > buf_cap) {
            *out_len = pos;
            return ROBO_ERROR;
        }
        buf[pos] = '\0';
    }
    *out_len = pos;
    return ROBO_OK;
}

robo_result_t robo_i18n_currency_formatter_init(
    robo_i18n_currency_formatter_t *fmt,
    const robo_i18n_locale_t *locale,
    const robo_i18n_currency_options_t *opts)
{
    robo_i18n_number_options_t nopts;
    robo_i18n_currency_options_t local_opts;
    char code[4];
    char cf[16];
    robo_result_t r;

    if (!fmt || !locale) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(fmt, 0, sizeof(*fmt));
    if (opts) {
        local_opts = *opts;
    } else {
        robo_i18n_currency_options_init(&local_opts);
    }
    fmt->locale = *locale;
    fmt->options = local_opts;

    if (local_opts.currency[0]) {
        r = robo_i18n_currency_normalize(local_opts.currency, strlen(local_opts.currency),
                                         code);
        if (r != ROBO_OK) {
            return r;
        }
    } else {
        r = robo_i18n_currency_for_locale(locale, code);
        if (r != ROBO_OK) {
            /* Last resort: XXX */
            memcpy(code, "XXX", 4u);
        }
    }
    memcpy(fmt->resolved_currency, code, 4u);
    r = robo_i18n_currency_lookup(code, 3u, &fmt->info);
    if (r != ROBO_OK && r != ROBO_NOT_FOUND) {
        return r;
    }

    fmt->accounting = 0u;
    if (local_opts.style == ROBO_I18N_CURRENCY_STYLE_ACCOUNTING) {
        fmt->accounting = 1u;
    } else if (read_keyword_from_other(locale, "cf", cf, sizeof(cf))) {
        if (strcmp(cf, "account") == 0) {
            fmt->accounting = 1u;
        }
    }

    robo_i18n_number_options_init(&nopts);
    nopts.rounding = local_opts.rounding;
    nopts.use_grouping = 1;
    nopts.min_fraction_digits = (int32_t)effective_digits(&fmt->info, local_opts.usage);
    nopts.max_fraction_digits = nopts.min_fraction_digits;
    copy_id(nopts.numbering_system, sizeof(nopts.numbering_system),
            local_opts.numbering_system);

    r = robo_i18n_number_formatter_init(&fmt->number, locale, &nopts);
    if (r != ROBO_OK) {
        return r;
    }
    fmt->ready = 1u;
    return ROBO_OK;
}

void robo_i18n_currency_formatter_destroy(robo_i18n_currency_formatter_t *fmt)
{
    if (!fmt) {
        return;
    }
    robo_i18n_number_formatter_destroy(&fmt->number);
    memset(fmt, 0, sizeof(*fmt));
}

const char *robo_i18n_currency_formatter_code(const robo_i18n_currency_formatter_t *fmt)
{
    return fmt && fmt->ready ? fmt->resolved_currency : NULL;
}

const robo_i18n_currency_info_t *robo_i18n_currency_formatter_info(
    const robo_i18n_currency_formatter_t *fmt)
{
    return fmt && fmt->ready ? &fmt->info : NULL;
}

robo_result_t robo_i18n_currency_format(const robo_i18n_currency_formatter_t *fmt,
                                        const robo_i18n_decimal_t *value,
                                        char *buf,
                                        size_t buf_cap,
                                        size_t *out_len)
{
    robo_i18n_decimal_t work;
    char numbuf[128];
    char currbuf[192];
    size_t num_len = 0u;
    size_t curr_len = 0u;
    size_t want = 0u;
    const robo_curr_fmt_t *cf;
    const char *pattern;
    const char *pat_pos;
    const char *semi;
    int negative;
    int prepend_minus = 0;
    int digits;
    int inc;
    robo_result_t r;
    robo_i18n_number_formatter_t nfmt;
    robo_i18n_number_options_t nopts;

    if (!fmt || !fmt->ready || !value || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_len = 0u;
    work = *value;

    digits = (int)effective_digits(&fmt->info, fmt->options.usage);
    inc = effective_rounding_inc(&fmt->info, fmt->options.usage);
    if (fmt->options.usage == ROBO_I18N_CURRENCY_USAGE_CASH && inc > 1) {
        r = apply_increment_rounding(&work, digits, inc, fmt->options.rounding);
        if (r != ROBO_OK) {
            return r;
        }
    }

    /* Format magnitude with currency fraction digits. */
    nfmt = fmt->number;
    robo_i18n_number_options_init(&nopts);
    nopts = fmt->number.options;
    nopts.min_fraction_digits = digits;
    nopts.max_fraction_digits = digits;
    nopts.show_plus = 0;
    /* Re-init local copy for digit options — reuse resolved symbols from fmt. */
    nfmt.options = nopts;

    negative = (work.kind == ROBO_I18N_DECIMAL_NUMBER && work.negative) ||
               work.kind == ROBO_I18N_DECIMAL_NEG_INFINITY;
    if (work.kind == ROBO_I18N_DECIMAL_NUMBER && work.negative) {
        work.negative = 0u; /* pattern / prepend handles sign */
    }

    r = robo_i18n_number_format(&nfmt, &work, numbuf, sizeof(numbuf), &num_len);
    if (r != ROBO_OK) {
        return r;
    }

    r = resolve_currency_text(fmt, value, currbuf, sizeof(currbuf), &curr_len);
    if (r != ROBO_OK && r != ROBO_ERROR) {
        return r;
    }
    if (r == ROBO_ERROR) {
        return r;
    }

    /* NAME style uses unitPattern, not ¤ pattern. */
    if (fmt->options.style == ROBO_I18N_CURRENCY_STYLE_NAME) {
        robo_i18n_plural_operands_t ops;
        robo_i18n_plural_category_t cat = ROBO_I18N_PLURAL_OTHER;
        const char *up = "{0} {1}";
        int idx;

        cf = resolve_fmt(&fmt->locale, fmt->number.numbering_system);
        (void)robo_i18n_plural_operands_from_decimal(value, &ops);
        (void)robo_i18n_plural_select(&fmt->locale, ROBO_I18N_PLURAL_CARDINAL, &ops,
                                      &cat);
        if (cf) {
            idx = (int)cat;
            if (idx < 0 || idx > 5) {
                idx = 5;
            }
            if (cf->unit_pattern[idx] >= 0) {
                up = robo_curr_string(cf->unit_pattern[idx]);
            } else if (cf->unit_pattern[5] >= 0) {
                up = robo_curr_string(cf->unit_pattern[5]);
            }
            if (!up) {
                up = "{0} {1}";
            }
        }
        r = expand_unit_pattern(up, numbuf, num_len, currbuf, curr_len, buf, buf_cap,
                                &want);
        if (negative && value->kind == ROBO_I18N_DECIMAL_NUMBER) {
            /* Prepend minus to NAME style when not already signed. */
            char tmp[384];
            size_t tl = 0u;
            const char *minus = fmt->number.minus_sign ? fmt->number.minus_sign : "-";
            if (r != ROBO_OK && r != ROBO_ERROR) {
                return r;
            }
            /* Re-expand into tmp then prefix — only when writing */
            r = expand_unit_pattern(up, numbuf, num_len, currbuf, curr_len, tmp,
                                    sizeof(tmp), &tl);
            if (r != ROBO_OK) {
                return r;
            }
            {
                size_t mlen = strlen(minus);
                size_t pos = 0u;
                int writing = (buf != NULL && buf_cap > 0u);
                robo_result_t ar;
                ar = append_bytes(buf, buf_cap, &pos, minus, mlen, writing);
                if (ar != ROBO_OK && ar != ROBO_ERROR) {
                    return ar;
                }
                ar = append_bytes(buf, buf_cap, &pos, tmp, tl, writing);
                if (ar != ROBO_OK && ar != ROBO_ERROR) {
                    return ar;
                }
                if (writing) {
                    if (pos + 1u > buf_cap) {
                        *out_len = pos;
                        return ROBO_ERROR;
                    }
                    buf[pos] = '\0';
                }
                *out_len = pos;
                return ROBO_OK;
            }
        }
        *out_len = want;
        return r;
    }

    cf = resolve_fmt(&fmt->locale, fmt->number.numbering_system);
    if (!cf) {
        /* Minimal fallback: code + space + number */
        size_t pos = 0u;
        int writing = (buf != NULL && buf_cap > 0u);
        if (negative) {
            const char *minus = fmt->number.minus_sign ? fmt->number.minus_sign : "-";
            r = append_bytes(buf, buf_cap, &pos, minus, strlen(minus), writing);
        }
        r = append_bytes(buf, buf_cap, &pos, currbuf, curr_len, writing);
        r = append_bytes(buf, buf_cap, &pos, "\xC2\xA0", 2u, writing);
        r = append_bytes(buf, buf_cap, &pos, numbuf, num_len, writing);
        if (writing && pos + 1u <= buf_cap) {
            buf[pos] = '\0';
        }
        *out_len = pos;
        return (writing && pos + 1u > buf_cap) ? ROBO_ERROR : ROBO_OK;
    }

    pattern = robo_curr_string(fmt->accounting ? cf->accounting : cf->standard);
    if (symbol_is_alphanumeric(currbuf) ||
        fmt->options.style == ROBO_I18N_CURRENCY_STYLE_CODE) {
        int32_t alt = fmt->accounting ? cf->accounting_alpha : cf->standard_alpha;
        if (alt >= 0) {
            const char *ap = robo_curr_string(alt);
            if (ap && ap[0]) {
                pattern = ap;
            }
        }
    }
    if (!pattern) {
        return ROBO_ERROR;
    }

    semi = strchr(pattern, ';');
    if (semi) {
        char posbuf[160];
        if (negative) {
            pat_pos = semi + 1;
            prepend_minus = 0;
        } else {
            size_t n = (size_t)(semi - pattern);
            if (n >= sizeof(posbuf)) {
                n = sizeof(posbuf) - 1u;
            }
            memcpy(posbuf, pattern, n);
            posbuf[n] = '\0';
            pat_pos = posbuf;
            prepend_minus = 0;
            return expand_currency_pattern(fmt, pat_pos, numbuf, num_len, currbuf,
                                           curr_len, prepend_minus, buf, buf_cap,
                                           out_len);
        }
    } else {
        pat_pos = pattern;
        prepend_minus = negative ? 1 : 0;
    }

    return expand_currency_pattern(fmt, pat_pos, numbuf, num_len, currbuf, curr_len,
                                   prepend_minus, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_currency_format_i64(const robo_i18n_currency_formatter_t *fmt,
                                            int64_t major_units,
                                            char *buf,
                                            size_t buf_cap,
                                            size_t *out_len)
{
    robo_i18n_decimal_t dec;
    robo_result_t r = robo_i18n_decimal_from_i64(major_units, &dec);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_currency_format(fmt, &dec, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_currency_format_minor(const robo_i18n_currency_formatter_t *fmt,
                                              int64_t minor_units,
                                              char *buf,
                                              size_t buf_cap,
                                              size_t *out_len)
{
    robo_i18n_decimal_t dec;
    char digs[32];
    size_t dn = 0u;
    int negative = 0;
    int64_t v;
    int digits;

    if (!fmt || !fmt->ready || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    digits = (int)effective_digits(&fmt->info, fmt->options.usage);
    v = minor_units;
    if (v < 0) {
        negative = 1;
        if (v == INT64_MIN) {
            return ROBO_NOT_SUPPORTED;
        }
        v = -v;
    }
    if (v == 0) {
        digs[dn++] = '0';
    } else {
        char rev[32];
        size_t rn = 0u;
        while (v > 0 && rn < sizeof(rev)) {
            rev[rn++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (rn > 0u) {
            digs[dn++] = rev[--rn];
        }
    }
    while (dn <= (size_t)digits && dn < sizeof(digs)) {
        memmove(digs + 1u, digs, dn);
        digs[0] = '0';
        dn++;
    }
    if (dn > ROBO_I18N_DECIMAL_DIGITS_MAX) {
        return ROBO_NOT_SUPPORTED;
    }
    memset(&dec, 0, sizeof(dec));
    dec.kind = ROBO_I18N_DECIMAL_NUMBER;
    dec.negative = (uint8_t)negative;
    dec.scale = (uint8_t)digits;
    dec.digit_count = (uint8_t)dn;
    memcpy(dec.digits, digs, dn);
    return robo_i18n_currency_format(fmt, &dec, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_currency_format_double(const robo_i18n_currency_formatter_t *fmt,
                                               double value,
                                               char *buf,
                                               size_t buf_cap,
                                               size_t *out_len)
{
    robo_i18n_decimal_t dec;
    robo_result_t r = robo_i18n_decimal_from_double(value, &dec);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_currency_format(fmt, &dec, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_currency_format_to_string(
    const robo_i18n_currency_formatter_t *fmt,
    const robo_i18n_decimal_t *value,
    robo_string_t *out)
{
    char stack[256];
    size_t len = 0u;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_currency_format(fmt, value, stack, sizeof(stack), &len);
    if (r == ROBO_OK) {
        return robo_string_append(out, stack, len);
    }
    if (r == ROBO_ERROR) {
        char big[2048];
        r = robo_i18n_currency_format(fmt, value, big, sizeof(big), &len);
        if (r != ROBO_OK) {
            return r;
        }
        return robo_string_append(out, big, len);
    }
    return r;
}

const char *robo_i18n_currency_cldr_version_string(void)
{
    return robo_curr_cldr_version_string();
}
