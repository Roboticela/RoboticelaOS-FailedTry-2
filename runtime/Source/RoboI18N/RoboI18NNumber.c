/*
 * CLDR-backed decimal number formatting (Goal 4.3).
 */

#include "RoboI18N/RoboI18NNumber.h"

#include "RoboI18N/RoboCldrData.h"
#include "RoboI18N/RoboI18NLocaleData.h"

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

static void format_tag_simple(const robo_i18n_locale_t *loc, char *buf, size_t cap)
{
    size_t n = 0;
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
    if (loc->script[0]) {
        if (n + 5u >= cap) {
            return;
        }
        buf[n++] = '-';
        memcpy(buf + n, loc->script, 4u);
        n += 4u;
    }
    if (loc->region[0]) {
        size_t rn = strlen(loc->region);
        if (n + 1u + rn >= cap) {
            return;
        }
        buf[n++] = '-';
        memcpy(buf + n, loc->region, rn);
        n += rn;
    }
    buf[n] = '\0';
}

void robo_i18n_number_options_init(robo_i18n_number_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->min_integer_digits = 1;
    opts->min_fraction_digits = 0;
    opts->max_fraction_digits = -1;
    opts->use_grouping = 1;
    opts->show_plus = 0;
    opts->rounding = ROBO_I18N_ROUND_HALF_EVEN;
}

static const robo_cldr_numfmt_t *lookup_numfmt(const robo_i18n_locale_t *locale,
                                               const char *nsys_id)
{
    robo_i18n_locale_t cur;
    int guard = 0;
    int32_t nsys_key;

    if (!locale || !nsys_id || nsys_id[0] == '\0') {
        return NULL;
    }
    nsys_key = robo_cldr_find_str(nsys_id);
    if (nsys_key < 0) {
        return NULL;
    }

    cur = *locale;
    cur.extensions[0] = '\0';
    cur.privateuse[0] = '\0';
    cur.variant_count = 0;

    while (guard++ < 16) {
        char tag[ROBO_I18N_TAG_MAX];
        int32_t loc_key;
        const robo_cldr_numfmt_t *row;
        robo_i18n_locale_t parent;

        if (strcmp(cur.language, "root") == 0) {
            loc_key = robo_cldr_find_str("und");
            if (loc_key >= 0) {
                row = robo_cldr_numfmt_find(loc_key, nsys_key);
                if (row) {
                    return row;
                }
            }
            break;
        }
        format_tag_simple(&cur, tag, sizeof(tag));
        loc_key = robo_cldr_find_str(tag);
        if (loc_key >= 0) {
            row = robo_cldr_numfmt_find(loc_key, nsys_key);
            if (row) {
                return row;
            }
        }
        if (robo_i18n_locale_data_parent(&cur, &parent) != ROBO_OK) {
            break;
        }
        cur = parent;
    }
    return NULL;
}

robo_result_t robo_i18n_number_formatter_init(robo_i18n_number_formatter_t *fmt,
                                              const robo_i18n_locale_t *locale,
                                              const robo_i18n_number_options_t *opts)
{
    robo_i18n_number_options_t local_opts;
    char nsys[16];
    const robo_cldr_numfmt_t *row;
    const robo_cldr_nsys_t *ns;
    int32_t nsys_key;

    if (!fmt || !locale || locale->language[0] == '\0') {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(fmt, 0, sizeof(*fmt));
    if (opts) {
        local_opts = *opts;
    } else {
        robo_i18n_number_options_init(&local_opts);
    }
    if (local_opts.min_integer_digits < 1) {
        local_opts.min_integer_digits = 1;
    }
    if (local_opts.min_integer_digits > (int32_t)ROBO_I18N_DECIMAL_DIGITS_MAX) {
        local_opts.min_integer_digits = (int32_t)ROBO_I18N_DECIMAL_DIGITS_MAX;
    }
    if (local_opts.min_fraction_digits < 0) {
        local_opts.min_fraction_digits = 0;
    }
    if (local_opts.max_fraction_digits >= 0 &&
        local_opts.min_fraction_digits > local_opts.max_fraction_digits) {
        local_opts.min_fraction_digits = local_opts.max_fraction_digits;
    }

    fmt->locale = *locale;
    fmt->options = local_opts;

    if (local_opts.numbering_system[0]) {
        copy_id(nsys, sizeof(nsys), local_opts.numbering_system);
    } else if (robo_i18n_locale_default_numbering_system(locale, nsys, sizeof(nsys)) !=
               ROBO_OK) {
        copy_id(nsys, sizeof(nsys), "latn");
    }
    copy_id(fmt->numbering_system, sizeof(fmt->numbering_system), nsys);

    nsys_key = robo_cldr_find_str(nsys);
    if (nsys_key < 0) {
        return ROBO_NOT_FOUND;
    }
    ns = robo_cldr_nsys_find(nsys_key);
    if (!ns) {
        return ROBO_NOT_FOUND;
    }
    fmt->digit_glyphs = robo_cldr_str(ns->digits);

    row = lookup_numfmt(locale, nsys);
    if (!row && strcmp(nsys, "latn") != 0) {
        row = lookup_numfmt(locale, "latn");
    }
    if (!row) {
        int32_t und = robo_cldr_find_str("und");
        int32_t latn = robo_cldr_find_str("latn");
        if (und >= 0 && latn >= 0) {
            row = robo_cldr_numfmt_find(und, latn);
        }
    }
    if (!row) {
        return ROBO_NOT_FOUND;
    }

    fmt->decimal_sep = robo_cldr_str(row->decimal);
    fmt->group_sep = robo_cldr_str(row->group);
    fmt->plus_sign = robo_cldr_str(row->plus_sign);
    fmt->minus_sign = robo_cldr_str(row->minus_sign);
    fmt->nan_symbol = robo_cldr_str(row->nan);
    fmt->infinity_symbol = robo_cldr_str(row->infinity);
    fmt->min_grouping = row->min_grouping;
    fmt->primary_group = row->primary_group;
    fmt->secondary_group = row->secondary_group;
    fmt->pattern_max_fraction = row->pattern_max_fraction;
    fmt->ready = 1;
    return ROBO_OK;
}

void robo_i18n_number_formatter_destroy(robo_i18n_number_formatter_t *fmt)
{
    if (fmt) {
        memset(fmt, 0, sizeof(*fmt));
    }
}

robo_result_t robo_i18n_decimal_from_u64(uint64_t value, robo_i18n_decimal_t *out)
{
    char tmp[24];
    size_t n = 0;
    uint64_t v;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->kind = ROBO_I18N_DECIMAL_NUMBER;
    if (value == 0) {
        out->digits[0] = '0';
        out->digit_count = 1;
        return ROBO_OK;
    }
    v = value;
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10ULL));
        v /= 10ULL;
    }
    if (n > ROBO_I18N_DECIMAL_DIGITS_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    while (n > 0) {
        out->digits[out->digit_count++] = tmp[--n];
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_decimal_from_i64(int64_t value, robo_i18n_decimal_t *out)
{
    uint64_t mag;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (value < 0) {
        mag = (uint64_t)(-(value + 1)) + 1ULL;
        r = robo_i18n_decimal_from_u64(mag, out);
        if (r != ROBO_OK) {
            return r;
        }
        out->negative = 1;
        return ROBO_OK;
    }
    return robo_i18n_decimal_from_u64((uint64_t)value, out);
}

static uint64_t double_bits(double x)
{
    uint64_t u;
    memcpy(&u, &x, sizeof(u));
    return u;
}

static int double_is_nan(uint64_t bits)
{
    return ((bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) &&
           ((bits & 0x000fffffffffffffULL) != 0ULL);
}

static int double_is_inf(uint64_t bits)
{
    return ((bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) &&
           ((bits & 0x000fffffffffffffULL) == 0ULL);
}

robo_result_t robo_i18n_decimal_from_double(double value, robo_i18n_decimal_t *out)
{
    uint64_t bits;
    int neg;
    double ax;
    size_t i;
    size_t n;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    bits = double_bits(value);
    neg = (bits >> 63) != 0;

    if (double_is_nan(bits)) {
        out->kind = ROBO_I18N_DECIMAL_NAN;
        return ROBO_OK;
    }
    if (double_is_inf(bits)) {
        out->kind = neg ? ROBO_I18N_DECIMAL_NEG_INFINITY : ROBO_I18N_DECIMAL_POS_INFINITY;
        out->negative = (uint8_t)(neg ? 1 : 0);
        return ROBO_OK;
    }

    {
        uint64_t ab = bits & 0x7fffffffffffffffULL;
        memcpy(&ax, &ab, sizeof(ax));
    }

    if (ax == 0.0) {
        out->kind = ROBO_I18N_DECIMAL_NUMBER;
        out->negative = (uint8_t)(neg ? 1 : 0);
        out->digits[0] = '0';
        out->digit_count = 1;
        return ROBO_OK;
    }
    if (ax >= 1.0e18) {
        return ROBO_NOT_SUPPORTED;
    }

    {
        const int work_frac = 15;
        double scaled = ax;
        uint64_t ip;
        char tmp[24];

        for (i = 0; i < (size_t)work_frac; i++) {
            scaled *= 10.0;
        }
        if (scaled >= 1.0e19) {
            return ROBO_NOT_SUPPORTED;
        }
        ip = (uint64_t)scaled;
        if ((scaled - (double)ip) >= 0.5) {
            ip += 1ULL;
        }
        n = 0;
        if (ip == 0) {
            out->digits[0] = '0';
            out->digit_count = 1;
            out->scale = 0;
        } else {
            uint64_t v = ip;
            while (v > 0) {
                tmp[n++] = (char)('0' + (v % 10ULL));
                v /= 10ULL;
            }
            while (n > 0) {
                if (out->digit_count >= ROBO_I18N_DECIMAL_DIGITS_MAX) {
                    return ROBO_INVALID_ARGUMENT;
                }
                out->digits[out->digit_count++] = tmp[--n];
            }
            out->scale = (uint8_t)work_frac;
            while (out->digit_count > (size_t)out->scale + 1u && out->digits[0] == '0') {
                memmove(out->digits, out->digits + 1, out->digit_count - 1u);
                out->digit_count--;
            }
        }
        out->kind = ROBO_I18N_DECIMAL_NUMBER;
        out->negative = (uint8_t)(neg ? 1 : 0);
        return ROBO_OK;
    }
}

static int digit_glyph_at(const char *glyphs, int digit, const char **out, size_t *out_len)
{
    size_t i;
    size_t start = 0;
    size_t idx = 0;
    static const char latn[10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

    if (digit < 0 || digit > 9) {
        return 0;
    }
    if (!glyphs || glyphs[0] == '\0') {
        *out = &latn[digit];
        *out_len = 1;
        return 1;
    }
    for (i = 0; glyphs[i];) {
        unsigned char c = (unsigned char)glyphs[i];
        size_t clen = 1;
        if (c >= 0xF0u) {
            clen = 4;
        } else if (c >= 0xE0u) {
            clen = 3;
        } else if (c >= 0xC0u) {
            clen = 2;
        }
        if (idx == (size_t)digit) {
            *out = glyphs + start;
            *out_len = clen;
            return 1;
        }
        i += clen;
        start = i;
        idx++;
    }
    return 0;
}

static int clamp_max_frac(const robo_i18n_number_formatter_t *fmt)
{
    if (fmt->options.max_fraction_digits < 0) {
        return (int)fmt->pattern_max_fraction;
    }
    return (int)fmt->options.max_fraction_digits;
}

static int should_round_up(int rounding,
                           int negative,
                           int digit,
                           int sticky_nonzero,
                           int least_kept_odd)
{
    switch ((robo_i18n_rounding_t)rounding) {
    case ROBO_I18N_ROUND_DOWN:
        return 0;
    case ROBO_I18N_ROUND_UP:
        return digit > 0 || sticky_nonzero;
    case ROBO_I18N_ROUND_CEILING:
        return !negative && (digit > 0 || sticky_nonzero);
    case ROBO_I18N_ROUND_FLOOR:
        return negative && (digit > 0 || sticky_nonzero);
    case ROBO_I18N_ROUND_HALF_DOWN:
        if (digit > 5) {
            return 1;
        }
        if (digit < 5) {
            return 0;
        }
        return sticky_nonzero;
    case ROBO_I18N_ROUND_HALF_UP:
        return digit >= 5;
    case ROBO_I18N_ROUND_HALF_EVEN:
    default:
        if (digit > 5) {
            return 1;
        }
        if (digit < 5) {
            return 0;
        }
        if (sticky_nonzero) {
            return 1;
        }
        return least_kept_odd;
    }
}

static robo_result_t round_decimal(const robo_i18n_decimal_t *in,
                                   int max_frac,
                                   int rounding,
                                   robo_i18n_decimal_t *out)
{
    char digits[ROBO_I18N_DECIMAL_DIGITS_MAX + 4];
    size_t count;
    size_t int_len;
    size_t i;
    int first_discard;
    int sticky;
    int least_odd;
    size_t keep;

    if (!in || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = *in;
    if (in->kind != ROBO_I18N_DECIMAL_NUMBER) {
        return ROBO_OK;
    }
    if (max_frac < 0) {
        max_frac = 0;
    }
    if (max_frac > (int)ROBO_I18N_DECIMAL_DIGITS_MAX - 1) {
        max_frac = (int)ROBO_I18N_DECIMAL_DIGITS_MAX - 1;
    }

    count = in->digit_count;
    if (count == 0) {
        out->digits[0] = '0';
        out->digit_count = 1;
        out->scale = 0;
        return ROBO_OK;
    }
    memcpy(digits, in->digits, count);
    int_len = (count > in->scale) ? (count - in->scale) : 0;

    if ((int)in->scale <= max_frac) {
        out->scale = in->scale;
        return ROBO_OK;
    }

    keep = int_len + (size_t)max_frac;
    if (keep == 0) {
        first_discard = digits[0] - '0';
        sticky = 0;
        for (i = 1; i < count; i++) {
            if (digits[i] != '0') {
                sticky = 1;
                break;
            }
        }
        if (should_round_up(rounding, in->negative, first_discard, sticky, 0)) {
            out->digits[0] = '1';
            out->digit_count = 1;
            out->scale = 0;
        } else {
            out->digits[0] = '0';
            out->digit_count = 1;
            out->scale = 0;
            out->negative = 0;
        }
        return ROBO_OK;
    }

    first_discard = (keep < count) ? (digits[keep] - '0') : 0;
    sticky = 0;
    for (i = keep + 1u; i < count; i++) {
        if (digits[i] != '0') {
            sticky = 1;
            break;
        }
    }
    least_odd = (digits[keep - 1u] - '0') & 1;

    if (should_round_up(rounding, in->negative, first_discard, sticky, least_odd)) {
        i = keep;
        while (i > 0) {
            i--;
            if (digits[i] < '9') {
                digits[i]++;
                break;
            }
            digits[i] = '0';
            if (i == 0) {
                if (keep + 1u > ROBO_I18N_DECIMAL_DIGITS_MAX) {
                    return ROBO_INVALID_ARGUMENT;
                }
                memmove(digits + 1, digits, keep);
                digits[0] = '1';
                keep++;
                break;
            }
        }
    }

    out->digit_count = (uint8_t)keep;
    memcpy(out->digits, digits, keep);
    out->scale = (uint8_t)max_frac;
    while (out->digit_count > out->scale + 1u && out->digits[0] == '0') {
        memmove(out->digits, out->digits + 1, out->digit_count - 1u);
        out->digit_count--;
    }
    return ROBO_OK;
}

static robo_result_t append_bytes(char *buf,
                                  size_t buf_cap,
                                  size_t *pos,
                                  const char *bytes,
                                  size_t len,
                                  int writing)
{
    if (!bytes && len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (writing) {
        if (*pos + len + 1u > buf_cap) {
            return ROBO_ERROR;
        }
        memcpy(buf + *pos, bytes, len);
    }
    *pos += len;
    return ROBO_OK;
}

static int sep_before_index(size_t index,
                            size_t int_len,
                            uint8_t primary,
                            uint8_t secondary)
{
    size_t pos;
    uint8_t step;
    if (index == 0 || primary == 0) {
        return 0;
    }
    pos = int_len - (size_t)primary;
    step = secondary ? secondary : primary;
    while (pos > 0) {
        if (pos == index) {
            return 1;
        }
        if (pos < step) {
            break;
        }
        pos -= step;
    }
    return 0;
}

static robo_result_t emit_integer_grouped(const robo_i18n_number_formatter_t *fmt,
                                          const char *ascii_int,
                                          size_t int_len,
                                          char *buf,
                                          size_t buf_cap,
                                          size_t *pos,
                                          int writing)
{
    size_t i;
    int use_group;
    robo_result_t r;

    use_group = fmt->options.use_grouping && fmt->primary_group > 0 &&
                int_len >= (size_t)fmt->primary_group + (size_t)fmt->min_grouping;

    for (i = 0; i < int_len; i++) {
        const char *g;
        size_t glen;
        if (use_group &&
            sep_before_index(i, int_len, fmt->primary_group, fmt->secondary_group)) {
            r = append_bytes(buf, buf_cap, pos, fmt->group_sep, strlen(fmt->group_sep),
                             writing);
            if (r != ROBO_OK) {
                return r;
            }
        }
        if (!digit_glyph_at(fmt->digit_glyphs, ascii_int[i] - '0', &g, &glen)) {
            return ROBO_ERROR;
        }
        r = append_bytes(buf, buf_cap, pos, g, glen, writing);
        if (r != ROBO_OK) {
            return r;
        }
    }
    return ROBO_OK;
}

static robo_result_t format_inner(const robo_i18n_number_formatter_t *fmt,
                                  const robo_i18n_decimal_t *value,
                                  char *buf,
                                  size_t buf_cap,
                                  size_t *out_len,
                                  int writing)
{
    robo_i18n_decimal_t rounded;
    int max_frac;
    int min_frac;
    int min_int;
    char int_digits[ROBO_I18N_DECIMAL_DIGITS_MAX + 4];
    char frac_digits[ROBO_I18N_DECIMAL_DIGITS_MAX + 4];
    size_t int_len = 0;
    size_t frac_len = 0;
    size_t pos = 0;
    robo_result_t r;

    if (value->kind == ROBO_I18N_DECIMAL_NAN) {
        size_t need = strlen(fmt->nan_symbol);
        if (out_len) {
            *out_len = need;
        }
        if (!writing) {
            return ROBO_OK;
        }
        if (need + 1u > buf_cap) {
            return ROBO_ERROR;
        }
        memcpy(buf, fmt->nan_symbol, need);
        buf[need] = '\0';
        return ROBO_OK;
    }
    if (value->kind == ROBO_I18N_DECIMAL_POS_INFINITY ||
        value->kind == ROBO_I18N_DECIMAL_NEG_INFINITY) {
        const char *sign = "";
        size_t sl = 0;
        size_t il = strlen(fmt->infinity_symbol);
        size_t need;
        if (value->kind == ROBO_I18N_DECIMAL_NEG_INFINITY) {
            sign = fmt->minus_sign;
            sl = strlen(sign);
        } else if (fmt->options.show_plus) {
            sign = fmt->plus_sign;
            sl = strlen(sign);
        }
        need = sl + il;
        if (out_len) {
            *out_len = need;
        }
        if (!writing) {
            return ROBO_OK;
        }
        if (need + 1u > buf_cap) {
            return ROBO_ERROR;
        }
        memcpy(buf, sign, sl);
        memcpy(buf + sl, fmt->infinity_symbol, il);
        buf[need] = '\0';
        return ROBO_OK;
    }

    max_frac = clamp_max_frac(fmt);
    min_frac = fmt->options.min_fraction_digits;
    if (min_frac > max_frac) {
        min_frac = max_frac;
    }
    min_int = fmt->options.min_integer_digits;

    r = round_decimal(value, max_frac, (int)fmt->options.rounding, &rounded);
    if (r != ROBO_OK) {
        return r;
    }

    {
        size_t scale = rounded.scale;
        size_t count = rounded.digit_count;
        size_t raw_int = (count > scale) ? (count - scale) : 0;
        size_t pad_int = (raw_int < (size_t)min_int) ? ((size_t)min_int - raw_int) : 0;
        size_t i;

        for (i = 0; i < pad_int; i++) {
            int_digits[int_len++] = '0';
        }
        if (raw_int == 0) {
            if (pad_int == 0) {
                int_digits[int_len++] = '0';
            }
        } else {
            memcpy(int_digits + int_len, rounded.digits, raw_int);
            int_len += raw_int;
        }

        if (scale > 0 && count > raw_int) {
            size_t fl = count - raw_int;
            memcpy(frac_digits, rounded.digits + raw_int, fl);
            frac_len = fl;
        }
        while ((int)frac_len < min_frac) {
            frac_digits[frac_len++] = '0';
        }
        if (max_frac == 0) {
            frac_len = 0;
        }
    }

    if (rounded.negative) {
        r = append_bytes(buf, buf_cap, &pos, fmt->minus_sign, strlen(fmt->minus_sign), writing);
    } else if (fmt->options.show_plus) {
        r = append_bytes(buf, buf_cap, &pos, fmt->plus_sign, strlen(fmt->plus_sign), writing);
    } else {
        r = ROBO_OK;
    }
    if (r != ROBO_OK) {
        return r;
    }

    r = emit_integer_grouped(fmt, int_digits, int_len, buf, buf_cap, &pos, writing);
    if (r != ROBO_OK) {
        return r;
    }

    if (frac_len > 0) {
        size_t i;
        r = append_bytes(buf, buf_cap, &pos, fmt->decimal_sep, strlen(fmt->decimal_sep), writing);
        if (r != ROBO_OK) {
            return r;
        }
        for (i = 0; i < frac_len; i++) {
            const char *g;
            size_t glen;
            if (!digit_glyph_at(fmt->digit_glyphs, frac_digits[i] - '0', &g, &glen)) {
                return ROBO_ERROR;
            }
            r = append_bytes(buf, buf_cap, &pos, g, glen, writing);
            if (r != ROBO_OK) {
                return r;
            }
        }
    }

    if (out_len) {
        *out_len = pos;
    }
    if (writing) {
        if (pos + 1u > buf_cap) {
            return ROBO_ERROR;
        }
        buf[pos] = '\0';
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_number_format(const robo_i18n_number_formatter_t *fmt,
                                      const robo_i18n_decimal_t *value,
                                      char *buf,
                                      size_t buf_cap,
                                      size_t *out_len)
{
    robo_result_t r;
    size_t needed = 0;

    if (!fmt || !fmt->ready || !value) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (buf == NULL || buf_cap == 0) {
        return format_inner(fmt, value, NULL, 0, out_len, 0);
    }
    r = format_inner(fmt, value, buf, buf_cap, out_len, 1);
    if (r == ROBO_ERROR) {
        (void)format_inner(fmt, value, NULL, 0, &needed, 0);
        if (out_len) {
            *out_len = needed;
        }
    }
    return r;
}

robo_result_t robo_i18n_number_format_i64(const robo_i18n_number_formatter_t *fmt,
                                          int64_t value,
                                          char *buf,
                                          size_t buf_cap,
                                          size_t *out_len)
{
    robo_i18n_decimal_t dec;
    robo_result_t r = robo_i18n_decimal_from_i64(value, &dec);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_number_format(fmt, &dec, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_number_format_u64(const robo_i18n_number_formatter_t *fmt,
                                          uint64_t value,
                                          char *buf,
                                          size_t buf_cap,
                                          size_t *out_len)
{
    robo_i18n_decimal_t dec;
    robo_result_t r = robo_i18n_decimal_from_u64(value, &dec);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_number_format(fmt, &dec, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_number_format_double(const robo_i18n_number_formatter_t *fmt,
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
    return robo_i18n_number_format(fmt, &dec, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_number_format_to_string(const robo_i18n_number_formatter_t *fmt,
                                                const robo_i18n_decimal_t *value,
                                                robo_string_t *out)
{
    char stack[256];
    size_t len = 0;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_number_format(fmt, value, stack, sizeof(stack), &len);
    if (r == ROBO_OK) {
        return robo_string_append(out, stack, len);
    }
    if (r == ROBO_ERROR) {
        char big[2048];
        r = robo_i18n_number_format(fmt, value, big, sizeof(big), &len);
        if (r != ROBO_OK) {
            return r;
        }
        return robo_string_append(out, big, len);
    }
    return r;
}
