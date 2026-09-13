/*
 * CLDR plural-rule evaluation (Goals 4.4 / 4.18).
 */

#include "RoboI18N/RoboI18NPlural.h"

#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboPluralData.h"

#include <string.h>

enum {
    PL_END = 0,
    PL_TRUE = 1,
    PL_AND = 2,
    PL_OR = 3,
    PL_TEST = 4,
    OP_N = 0,
    OP_I = 1,
    OP_V = 2,
    OP_W = 3,
    OP_F = 4,
    OP_T = 5,
    OP_E = 6
};

static const char *const CAT_NAMES[6] = {
    "zero", "one", "two", "few", "many", "other"
};

const char *robo_i18n_plural_category_name(robo_i18n_plural_category_t cat)
{
    if ((unsigned)cat > 5u) {
        return "";
    }
    return CAT_NAMES[cat];
}

robo_result_t robo_i18n_plural_category_from_name(const char *name,
                                                  robo_i18n_plural_category_t *out)
{
    size_t i;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!name || !name[0]) {
        *out = ROBO_I18N_PLURAL_OTHER;
        return ROBO_NOT_FOUND;
    }
    for (i = 0; i < 6u; i++) {
        if (strcmp(CAT_NAMES[i], name) == 0) {
            *out = (robo_i18n_plural_category_t)i;
            return ROBO_OK;
        }
    }
    *out = ROBO_I18N_PLURAL_OTHER;
    return ROBO_NOT_FOUND;
}

static void format_tag_lang(const robo_i18n_locale_t *loc, char *buf, size_t cap)
{
    size_t n;
    if (!loc || !buf || cap == 0) {
        return;
    }
    n = strlen(loc->language);
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(buf, loc->language, n);
    buf[n] = '\0';
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t digits_mod(const char *digits, size_t len, uint32_t mod)
{
    uint32_t acc = 0;
    size_t i;
    if (mod == 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        acc = (acc * 10u + (uint32_t)(digits[i] - '0')) % mod;
    }
    return acc;
}

static int digits_eq_u32(const char *digits, size_t len, uint32_t v)
{
    char tmp[16];
    size_t n = 0;
    uint32_t x = v;
    size_t i;
    if (v == 0) {
        return len == 1 && digits[0] == '0';
    }
    while (x > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (x % 10u));
        x /= 10u;
    }
    if (n != len) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (digits[i] != tmp[n - 1u - i]) {
            return 0;
        }
    }
    return 1;
}

static int frac_all_zero(const robo_i18n_plural_operands_t *ops)
{
    size_t i;
    for (i = 0; i < ops->v; i++) {
        if (ops->f_digits[i] != '0') {
            return 0;
        }
    }
    return 1;
}

static void trim_t(const robo_i18n_plural_operands_t *ops, char *out, size_t *out_len)
{
    size_t n = ops->v;
    while (n > 0 && ops->f_digits[n - 1u] == '0') {
        n--;
    }
    if (n == 0) {
        out[0] = '0';
        *out_len = 1;
        return;
    }
    memcpy(out, ops->f_digits, n);
    *out_len = n;
}

/*
 * Obtain an integer operand value for range tests.
 * For n: only defined as integer when fraction is all zeros (then equals i).
 * Returns 0 and sets *ok=0 if operand is not an integer for range matching.
 */
static int operand_int(const robo_i18n_plural_operands_t *ops,
                       int operand,
                       uint32_t mod,
                       uint32_t *out,
                       int *ok)
{
    char tbuf[ROBO_I18N_DECIMAL_DIGITS_MAX];
    size_t tlen = 0;
    uint32_t val = 0;
    *ok = 1;

    switch (operand) {
    case OP_V:
        val = ops->v;
        break;
    case OP_W:
        val = ops->w;
        break;
    case OP_E:
        val = ops->e;
        break;
    case OP_I:
        if (mod) {
            val = digits_mod(ops->i_digits, ops->i_len, mod);
        } else if (ops->i_len <= 9) {
            size_t i;
            for (i = 0; i < ops->i_len; i++) {
                val = val * 10u + (uint32_t)(ops->i_digits[i] - '0');
            }
        } else {
            /* Large integer: only support via mod or exact range match later */
            *ok = 0;
            return 0;
        }
        break;
    case OP_F:
        if (ops->v == 0) {
            val = 0;
        } else if (mod) {
            val = digits_mod(ops->f_digits, ops->v, mod);
        } else if (ops->v <= 9) {
            size_t i;
            for (i = 0; i < ops->v; i++) {
                val = val * 10u + (uint32_t)(ops->f_digits[i] - '0');
            }
        } else {
            *ok = 0;
            return 0;
        }
        break;
    case OP_T:
        trim_t(ops, tbuf, &tlen);
        if (mod) {
            val = digits_mod(tbuf, tlen, mod);
        } else if (tlen <= 9) {
            size_t i;
            for (i = 0; i < tlen; i++) {
                val = val * 10u + (uint32_t)(tbuf[i] - '0');
            }
        } else {
            *ok = 0;
            return 0;
        }
        break;
    case OP_N:
        if (!frac_all_zero(ops)) {
            *ok = 0;
            return 0;
        }
        if (mod) {
            val = digits_mod(ops->i_digits, ops->i_len, mod);
        } else if (ops->i_len <= 9) {
            size_t i;
            for (i = 0; i < ops->i_len; i++) {
                val = val * 10u + (uint32_t)(ops->i_digits[i] - '0');
            }
        } else {
            *ok = 0;
            return 0;
        }
        break;
    default:
        *ok = 0;
        return 0;
    }
    *out = val;
    return 1;
}

static int match_ranges_u32(uint32_t val, const uint8_t *p, uint8_t n_ranges)
{
    uint8_t i;
    for (i = 0; i < n_ranges; i++) {
        uint32_t lo = read_u32(p + (size_t)i * 8u);
        uint32_t hi = read_u32(p + (size_t)i * 8u + 4u);
        if (val >= lo && val <= hi) {
            return 1;
        }
    }
    return 0;
}

static int match_ranges_digits(const char *digits,
                               size_t len,
                               const uint8_t *p,
                               uint8_t n_ranges)
{
    uint8_t i;
    for (i = 0; i < n_ranges; i++) {
        uint32_t lo = read_u32(p + (size_t)i * 8u);
        uint32_t hi = read_u32(p + (size_t)i * 8u + 4u);
        uint32_t v;
        if (lo != hi) {
            /* Range on large digit string: only if len small enough */
            if (len > 9) {
                continue;
            }
            {
                size_t j;
                v = 0;
                for (j = 0; j < len; j++) {
                    v = v * 10u + (uint32_t)(digits[j] - '0');
                }
            }
            if (v >= lo && v <= hi) {
                return 1;
            }
        } else if (digits_eq_u32(digits, len, lo)) {
            return 1;
        }
    }
    return 0;
}

static int eval_test(const robo_i18n_plural_operands_t *ops, const uint8_t **pp)
{
    const uint8_t *p = *pp;
    int operand = p[0];
    int flags = p[1];
    uint32_t mod = read_u32(p + 2);
    uint8_t n_ranges = p[6];
    const uint8_t *ranges = p + 7;
    int negated = (flags & 1) != 0;
    int has_mod = (flags & 2) != 0;
    int matched = 0;
    int ok = 0;
    uint32_t val = 0;

    *pp = ranges + (size_t)n_ranges * 8u;

    if (!has_mod) {
        mod = 0;
    }

    /* Exact large-integer equality without mod for n/i */
    if (!has_mod && (operand == OP_N || operand == OP_I) &&
        (operand != OP_N || frac_all_zero(ops))) {
        matched = match_ranges_digits(ops->i_digits, ops->i_len, ranges, n_ranges);
        return negated ? !matched : matched;
    }

    if (!operand_int(ops, operand, has_mod ? mod : 0, &val, &ok) || !ok) {
        /* Non-integer n (or oversized): cannot match integer ranges. */
        matched = 0;
        return negated ? !matched : matched;
    }
    matched = match_ranges_u32(val, ranges, n_ranges);
    return negated ? !matched : matched;
}

static int eval_bytecode(const robo_i18n_plural_operands_t *ops, const uint8_t *code, size_t len)
{
    int stack[32];
    size_t sp = 0;
    const uint8_t *p = code;
    const uint8_t *end = code + len;

    while (p < end) {
        uint8_t op = *p++;
        if (op == PL_END) {
            break;
        }
        if (op == PL_TRUE) {
            if (sp >= 32) {
                return 0;
            }
            stack[sp++] = 1;
            continue;
        }
        if (op == PL_AND) {
            if (sp < 2) {
                return 0;
            }
            stack[sp - 2] = stack[sp - 2] && stack[sp - 1];
            sp--;
            continue;
        }
        if (op == PL_OR) {
            if (sp < 2) {
                return 0;
            }
            stack[sp - 2] = stack[sp - 2] || stack[sp - 1];
            sp--;
            continue;
        }
        if (op == PL_TEST) {
            if (p + 7 > end) {
                return 0;
            }
            {
                const uint8_t *q = p;
                int r = eval_test(ops, &q);
                if (q > end) {
                    return 0;
                }
                p = q;
                if (sp >= 32) {
                    return 0;
                }
                stack[sp++] = r;
            }
            continue;
        }
        return 0;
    }
    return sp == 1 && stack[0];
}

static const robo_plural_locale_index_t *lookup_kind(robo_i18n_plural_kind_t kind,
                                                     const char *tag)
{
    if (kind == ROBO_I18N_PLURAL_ORDINAL) {
        return robo_plural_ordinal_find(tag);
    }
    return robo_plural_cardinal_find(tag);
}

static void format_tag_lang_region(const robo_i18n_locale_t *loc, char *buf, size_t cap)
{
    size_t n;
    size_t ln;
    size_t rn;
    if (!loc || !buf || cap < 4) {
        return;
    }
    ln = strlen(loc->language);
    rn = strlen(loc->region);
    if (ln == 0 || rn == 0 || ln + 1 + rn >= cap) {
        buf[0] = '\0';
        return;
    }
    memcpy(buf, loc->language, ln);
    n = ln;
    buf[n++] = '-';
    memcpy(buf + n, loc->region, rn);
    n += rn;
    buf[n] = '\0';
}

static void format_tag_lang_script(const robo_i18n_locale_t *loc, char *buf, size_t cap)
{
    size_t n;
    size_t ln;
    if (!loc || !buf || cap < 8) {
        return;
    }
    ln = strlen(loc->language);
    if (ln == 0 || loc->script[0] == '\0' || ln + 5 >= cap) {
        buf[0] = '\0';
        return;
    }
    memcpy(buf, loc->language, ln);
    n = ln;
    buf[n++] = '-';
    memcpy(buf + n, loc->script, 4);
    n += 4;
    buf[n] = '\0';
}

static const robo_plural_locale_index_t *resolve_index(const robo_i18n_locale_t *locale,
                                                        robo_i18n_plural_kind_t kind)
{
    robo_i18n_locale_t cur;
    int guard = 0;
    char tag[ROBO_I18N_TAG_MAX];
    const robo_plural_locale_index_t *idx = NULL;

    if (!locale || locale->language[0] == '\0') {
        return NULL;
    }
    cur = *locale;
    cur.extensions[0] = '\0';
    cur.privateuse[0] = '\0';
    cur.variant_count = 0;

    while (guard++ < 16) {
        /* Most-specific CLDR keys first (e.g. pt-PT before pt). */
        format_tag_lang_region(&cur, tag, sizeof(tag));
        if (tag[0]) {
            idx = lookup_kind(kind, tag);
            if (idx) {
                return idx;
            }
        }
        format_tag_lang_script(&cur, tag, sizeof(tag));
        if (tag[0]) {
            idx = lookup_kind(kind, tag);
            if (idx) {
                return idx;
            }
        }
        format_tag_lang(&cur, tag, sizeof(tag));
        idx = lookup_kind(kind, tag);
        if (idx) {
            return idx;
        }
        if (strcmp(cur.language, "root") == 0) {
            break;
        }
        {
            robo_i18n_locale_t parent;
            if (robo_i18n_locale_data_parent(&cur, &parent) != ROBO_OK) {
                break;
            }
            cur = parent;
        }
    }
    return NULL;
}

robo_result_t robo_i18n_plural_operands_from_decimal(const robo_i18n_decimal_t *dec,
                                                     robo_i18n_plural_operands_t *out)
{
    size_t scale;
    size_t count;
    size_t raw_int;
    size_t i;

    if (!dec || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (dec->kind != ROBO_I18N_DECIMAL_NUMBER) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->negative_input = dec->negative;
    scale = dec->scale;
    count = dec->digit_count;
    if (count == 0) {
        out->i_digits[0] = '0';
        out->i_len = 1;
        out->v = 0;
        out->w = 0;
        return ROBO_OK;
    }
    raw_int = (count > scale) ? (count - scale) : 0;
    if (raw_int == 0) {
        out->i_digits[0] = '0';
        out->i_len = 1;
    } else {
        memcpy(out->i_digits, dec->digits, raw_int);
        out->i_len = (uint8_t)raw_int;
        /* strip leading zeros */
        while (out->i_len > 1u && out->i_digits[0] == '0') {
            memmove(out->i_digits, out->i_digits + 1, out->i_len - 1u);
            out->i_len--;
        }
    }
    out->v = (uint8_t)scale;
    if (scale > 0) {
        size_t have = (count > raw_int) ? (count - raw_int) : 0;
        if (have >= scale) {
            memcpy(out->f_digits, dec->digits + raw_int, scale);
        } else {
            if (have > 0) {
                memcpy(out->f_digits, dec->digits + raw_int, have);
            }
            for (i = have; i < scale; i++) {
                out->f_digits[i] = '0';
            }
        }
        /* w = visible fraction without trailing zeros */
        {
            size_t w = scale;
            while (w > 0 && out->f_digits[w - 1u] == '0') {
                w--;
            }
            out->w = (uint8_t)w;
        }
    }
    out->e = 0;
    return ROBO_OK;
}

robo_result_t robo_i18n_plural_operands_from_string(const char *text,
                                                    size_t len,
                                                    robo_i18n_plural_operands_t *out)
{
    robo_i18n_decimal_t dec;
    size_t i = 0;
    size_t start;
    size_t dot = (size_t)-1;
    int neg = 0;

    if (!text || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(&dec, 0, sizeof(dec));
    dec.kind = ROBO_I18N_DECIMAL_NUMBER;
    while (i < len && (text[i] == ' ' || text[i] == '\t')) {
        i++;
    }
    if (i < len && text[i] == '-') {
        neg = 1;
        i++;
    } else if (i < len && text[i] == '+') {
        i++;
    }
    start = i;
    for (; i < len; i++) {
        if (text[i] == '.') {
            if (dot != (size_t)-1) {
                return ROBO_PARSE_ERROR;
            }
            dot = i;
        } else if (text[i] < '0' || text[i] > '9') {
            break;
        }
    }
    if (i == start || (dot == start && i == start + 1)) {
        return ROBO_PARSE_ERROR;
    }
    {
        size_t int_start = start;
        size_t int_end = (dot == (size_t)-1) ? i : dot;
        size_t frac_start = (dot == (size_t)-1) ? i : dot + 1;
        size_t frac_end = i;
        size_t int_len = int_end - int_start;
        size_t frac_len = frac_end - frac_start;
        char digits[ROBO_I18N_DECIMAL_DIGITS_MAX];
        size_t n = 0;
        size_t j;

        if (int_len == 0) {
            digits[n++] = '0';
        } else {
            /* skip leading zeros but keep one */
            j = int_start;
            while (j + 1 < int_end && text[j] == '0') {
                j++;
            }
            while (j < int_end) {
                if (n >= ROBO_I18N_DECIMAL_DIGITS_MAX) {
                    return ROBO_INVALID_ARGUMENT;
                }
                digits[n++] = text[j++];
            }
        }
        for (j = frac_start; j < frac_end; j++) {
            if (n >= ROBO_I18N_DECIMAL_DIGITS_MAX) {
                return ROBO_INVALID_ARGUMENT;
            }
            digits[n++] = text[j];
        }
        dec.negative = (uint8_t)neg;
        dec.scale = (uint8_t)frac_len;
        dec.digit_count = (uint8_t)n;
        memcpy(dec.digits, digits, n);
    }
    return robo_i18n_plural_operands_from_decimal(&dec, out);
}

robo_result_t robo_i18n_plural_select(const robo_i18n_locale_t *locale,
                                     robo_i18n_plural_kind_t kind,
                                     const robo_i18n_plural_operands_t *ops,
                                     robo_i18n_plural_category_t *out_cat)
{
    const robo_plural_locale_index_t *idx;
    const uint8_t *p;
    const uint8_t *end;

    if (!locale || !ops || !out_cat) {
        return ROBO_INVALID_ARGUMENT;
    }
    idx = resolve_index(locale, kind);
    if (!idx) {
        *out_cat = ROBO_I18N_PLURAL_OTHER;
        return ROBO_OK;
    }
    p = robo_plural_bytecode + idx->offset;
    end = p + idx->length;
    while (p < end) {
        uint8_t cat = *p++;
        uint16_t blen;
        if (cat == 0xFF) {
            break;
        }
        if (p + 2 > end) {
            return ROBO_ERROR;
        }
        blen = (uint16_t)(p[0] | (p[1] << 8));
        p += 2;
        if (p + blen > end) {
            return ROBO_ERROR;
        }
        if (eval_bytecode(ops, p, blen)) {
            *out_cat = (robo_i18n_plural_category_t)cat;
            return ROBO_OK;
        }
        p += blen;
    }
    *out_cat = ROBO_I18N_PLURAL_OTHER;
    return ROBO_OK;
}

robo_result_t robo_i18n_plural_cardinal(const robo_i18n_locale_t *locale,
                                        const robo_i18n_decimal_t *number,
                                        robo_i18n_plural_category_t *out_cat)
{
    robo_i18n_plural_operands_t ops;
    robo_result_t r = robo_i18n_plural_operands_from_decimal(number, &ops);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_plural_select(locale, ROBO_I18N_PLURAL_CARDINAL, &ops, out_cat);
}

robo_result_t robo_i18n_plural_ordinal(const robo_i18n_locale_t *locale,
                                       const robo_i18n_decimal_t *number,
                                       robo_i18n_plural_category_t *out_cat)
{
    robo_i18n_plural_operands_t ops;
    robo_result_t r = robo_i18n_plural_operands_from_decimal(number, &ops);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_plural_select(locale, ROBO_I18N_PLURAL_ORDINAL, &ops, out_cat);
}

robo_result_t robo_i18n_plural_cardinal_i64(const robo_i18n_locale_t *locale,
                                            int64_t value,
                                            robo_i18n_plural_category_t *out_cat)
{
    robo_i18n_decimal_t dec;
    robo_result_t r = robo_i18n_decimal_from_i64(value, &dec);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_plural_cardinal(locale, &dec, out_cat);
}

robo_result_t robo_i18n_plural_ordinal_i64(const robo_i18n_locale_t *locale,
                                           int64_t value,
                                           robo_i18n_plural_category_t *out_cat)
{
    robo_i18n_decimal_t dec;
    robo_result_t r = robo_i18n_decimal_from_i64(value, &dec);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_plural_ordinal(locale, &dec, out_cat);
}

int robo_i18n_plural_has_rules(const robo_i18n_locale_t *locale,
                               robo_i18n_plural_kind_t kind)
{
    if (!locale || locale->language[0] == '\0') {
        return 0;
    }
    return resolve_index(locale, kind) != NULL;
}

robo_result_t robo_i18n_plural_categories(const robo_i18n_locale_t *locale,
                                          robo_i18n_plural_kind_t kind,
                                          robo_i18n_plural_category_t *out,
                                          size_t cap,
                                          size_t *out_count)
{
    const robo_plural_locale_index_t *idx;
    const uint8_t *p;
    const uint8_t *end;
    size_t n = 0;
    int saw_other = 0;

    if (!locale || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    idx = resolve_index(locale, kind);
    if (!idx) {
        if (cap > 0 && out) {
            out[0] = ROBO_I18N_PLURAL_OTHER;
        }
        *out_count = 1;
        return ROBO_OK;
    }
    p = robo_plural_bytecode + idx->offset;
    end = p + idx->length;
    while (p < end) {
        uint8_t cat = *p++;
        uint16_t blen;
        if (cat == 0xFF) {
            break;
        }
        if (p + 2 > end) {
            return ROBO_ERROR;
        }
        blen = (uint16_t)(p[0] | (p[1] << 8));
        p += 2 + blen;
        if (cat == (uint8_t)ROBO_I18N_PLURAL_OTHER) {
            saw_other = 1;
        }
        if (n < cap && out) {
            out[n] = (robo_i18n_plural_category_t)cat;
        }
        n++;
    }
    if (!saw_other) {
        if (n < cap && out) {
            out[n] = ROBO_I18N_PLURAL_OTHER;
        }
        n++;
    }
    *out_count = n;
    return ROBO_OK;
}
