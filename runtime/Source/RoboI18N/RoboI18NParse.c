/*
 * Locale-aware number & currency parsing (Goal 4.22).
 */

#include "RoboI18N/RoboI18NParse.h"

#include "RoboI18N/RoboCurrencyData.h"
#include "RoboI18N/RoboDispData.h"
#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeProps.h"

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

static char ascii_upper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static int ascii_is_alpha(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

void robo_i18n_parse_options_init(robo_i18n_parse_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->mode = ROBO_I18N_PARSE_STRICT;
    opts->allow_leading_ws = 1;
    opts->allow_trailing_ws = 1;
    opts->require_full = 1;
    opts->integer_only = 0;
}

void robo_i18n_parse_result_clear(robo_i18n_parse_result_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

void robo_i18n_currency_parse_options_init(robo_i18n_currency_parse_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    robo_i18n_parse_options_init(&opts->number);
    opts->allow_currency_names = 1;
}

const char *robo_i18n_parse_err_string(robo_i18n_parse_err_t err)
{
    switch (err) {
    case ROBO_I18N_PARSE_ERR_NONE:
        return "ok";
    case ROBO_I18N_PARSE_ERR_EMPTY:
        return "empty input";
    case ROBO_I18N_PARSE_ERR_INVALID_UTF8:
        return "invalid UTF-8";
    case ROBO_I18N_PARSE_ERR_INVALID_DIGIT:
        return "invalid digit";
    case ROBO_I18N_PARSE_ERR_INVALID_GROUPING:
        return "invalid grouping";
    case ROBO_I18N_PARSE_ERR_INVALID_DECIMAL:
        return "invalid decimal separator";
    case ROBO_I18N_PARSE_ERR_INVALID_SIGN:
        return "invalid sign";
    case ROBO_I18N_PARSE_ERR_INVALID_CURRENCY:
        return "invalid currency";
    case ROBO_I18N_PARSE_ERR_AMBIGUOUS_CURRENCY:
        return "ambiguous currency";
    case ROBO_I18N_PARSE_ERR_UNEXPECTED_CHAR:
        return "unexpected character";
    case ROBO_I18N_PARSE_ERR_TRAILING:
        return "trailing input";
    case ROBO_I18N_PARSE_ERR_OVERFLOW:
        return "overflow";
    case ROBO_I18N_PARSE_ERR_UNSUPPORTED_LOCALE:
        return "unsupported locale";
    case ROBO_I18N_PARSE_ERR_INPUT_TOO_LONG:
        return "input too long";
    default:
        return "unknown parse error";
    }
}

static void fail(robo_i18n_parse_result_t *out,
                 robo_i18n_parse_err_t err,
                 size_t offset)
{
    if (!out) {
        return;
    }
    out->error = err;
    out->error_offset = offset;
}

static int prefix_eq(const char *text, size_t len, size_t at, const char *lit)
{
    size_t n;
    if (!lit || !lit[0]) {
        return 0;
    }
    n = strlen(lit);
    if (at + n > len) {
        return 0;
    }
    return memcmp(text + at, lit, n) == 0;
}

static size_t skip_ws(const char *text, size_t len, size_t at)
{
    while (at < len) {
        robo_uchar32_t cp = 0;
        size_t next = at;
        if (robo_unicode_utf8_next(text, len, at, ROBO_UNICODE_STRICT, &cp, &next) !=
            ROBO_UNICODE_OK) {
            break;
        }
        if (!robo_unicode_is_whitespace(cp)) {
            break;
        }
        at = next;
    }
    return at;
}

static int match_digit(const robo_i18n_number_parser_t *p,
                       const char *text,
                       size_t len,
                       size_t at,
                       int *out_d,
                       size_t *out_next)
{
    int d;
    if (at >= len) {
        return 0;
    }
    for (d = 0; d < 10; d++) {
        if (p->digit_len[d] == 0u) {
            continue;
        }
        if (at + p->digit_len[d] <= len &&
            memcmp(text + at, p->digit_utf8[d], p->digit_len[d]) == 0) {
            *out_d = d;
            *out_next = at + p->digit_len[d];
            return 1;
        }
    }
    /* Always accept ASCII digits as a fallback for mixed input in permissive,
     * and when numbering system is latn. */
    if (text[at] >= '0' && text[at] <= '9') {
        *out_d = (int)(text[at] - '0');
        *out_next = at + 1u;
        return 1;
    }
    return 0;
}

static int build_digit_table(robo_i18n_number_parser_t *parser)
{
    const char *g;
    size_t glen;
    size_t off = 0u;
    int d;

    for (d = 0; d < 10; d++) {
        parser->digit_utf8[d] = NULL;
        parser->digit_len[d] = 0u;
    }
    g = parser->symbols.digit_glyphs;
    if (!g || !g[0]) {
        static const char ascii[] = "0123456789";
        for (d = 0; d < 10; d++) {
            parser->digit_utf8[d] = ascii + d;
            parser->digit_len[d] = 1u;
        }
        return 1;
    }
    glen = strlen(g);
    for (d = 0; d < 10; d++) {
        robo_uchar32_t cp = 0;
        size_t next = off;
        if (off >= glen) {
            return 0;
        }
        if (robo_unicode_utf8_next(g, glen, off, ROBO_UNICODE_STRICT, &cp, &next) !=
            ROBO_UNICODE_OK) {
            return 0;
        }
        parser->digit_utf8[d] = g + off;
        parser->digit_len[d] = (uint8_t)(next - off);
        off = next;
    }
    return 1;
}

robo_result_t robo_i18n_number_parser_init(robo_i18n_number_parser_t *parser,
                                           const robo_i18n_locale_t *locale,
                                           const robo_i18n_parse_options_t *opts)
{
    robo_i18n_number_options_t nopts;
    robo_result_t r;

    if (!parser || !locale) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(parser, 0, sizeof(*parser));
    if (opts) {
        parser->options = *opts;
    } else {
        robo_i18n_parse_options_init(&parser->options);
    }
    parser->locale = *locale;

    robo_i18n_number_options_init(&nopts);
    copy_id(nopts.numbering_system, sizeof(nopts.numbering_system),
            parser->options.numbering_system);
    r = robo_i18n_number_formatter_init(&parser->symbols, locale, &nopts);
    if (r != ROBO_OK) {
        return r == ROBO_NOT_FOUND ? ROBO_NOT_FOUND : r;
    }
    if (!build_digit_table(parser)) {
        robo_i18n_number_formatter_destroy(&parser->symbols);
        return ROBO_ERROR;
    }
    parser->ready = 1u;
    return ROBO_OK;
}

void robo_i18n_number_parser_destroy(robo_i18n_number_parser_t *parser)
{
    if (!parser) {
        return;
    }
    robo_i18n_number_formatter_destroy(&parser->symbols);
    memset(parser, 0, sizeof(*parser));
}

static int sep_eq(const char *sep, const char *text, size_t len, size_t at, size_t *next)
{
    size_t n;
    if (!sep || !sep[0]) {
        return 0;
    }
    n = strlen(sep);
    if (at + n > len) {
        return 0;
    }
    if (memcmp(text + at, sep, n) != 0) {
        return 0;
    }
    *next = at + n;
    return 1;
}

/**
 * Parse numeric body starting at @p at into @p out->value.
 * Updates *io_at on success. Grouping validated per mode.
 */
static robo_result_t parse_number_body(const robo_i18n_number_parser_t *parser,
                                       const char *text,
                                       size_t len,
                                       size_t *io_at,
                                       robo_i18n_parse_result_t *out,
                                       int allow_fraction)
{
    size_t at = *io_at;
    char digs[ROBO_I18N_DECIMAL_DIGITS_MAX];
    size_t dn = 0u;
    size_t frac_start = (size_t)-1;
    int saw_digit = 0;
    int saw_group = 0;
    size_t group_run = 0u; /* digits since last group / start */
    uint8_t primary = parser->symbols.primary_group;
    uint8_t secondary = parser->symbols.secondary_group;
    int strict = (parser->options.mode == ROBO_I18N_PARSE_STRICT);
    const char *dec = parser->symbols.decimal_sep;
    const char *grp = parser->symbols.group_sep;

    if (primary == 0u) {
        primary = 3u;
    }
    if (secondary == 0u) {
        secondary = primary;
    }

    while (at < len) {
        int d = 0;
        size_t next = at;
        size_t sep_next = at;

        if (match_digit(parser, text, len, at, &d, &next)) {
            if (dn >= ROBO_I18N_DECIMAL_DIGITS_MAX) {
                fail(out, ROBO_I18N_PARSE_ERR_OVERFLOW, at);
                return ROBO_PARSE_ERROR;
            }
            digs[dn++] = (char)('0' + d);
            saw_digit = 1;
            group_run++;
            at = next;
            continue;
        }

        if (allow_fraction && frac_start == (size_t)-1 &&
            sep_eq(dec, text, len, at, &sep_next)) {
            /* Decimal separator must not appear before any digit in strict
             * unless we allow ".5" in permissive. */
            if (!saw_digit) {
                if (strict) {
                    fail(out, ROBO_I18N_PARSE_ERR_INVALID_DECIMAL, at);
                    return ROBO_PARSE_ERROR;
                }
                digs[dn++] = '0';
                saw_digit = 1;
            }
            if (saw_group && strict) {
                /* Last group before decimal must be primary size. */
                if (group_run != (size_t)primary) {
                    fail(out, ROBO_I18N_PARSE_ERR_INVALID_GROUPING, at);
                    return ROBO_PARSE_ERROR;
                }
            }
            frac_start = dn;
            group_run = 0u;
            at = sep_next;
            continue;
        }

        if (frac_start == (size_t)-1 && sep_eq(grp, text, len, at, &sep_next)) {
            if (!saw_digit) {
                fail(out, ROBO_I18N_PARSE_ERR_INVALID_GROUPING, at);
                return ROBO_PARSE_ERROR;
            }
            if (strict) {
                if (!saw_group) {
                    /* First (most-significant) group: 1..secondary (or primary
                     * when no secondary distinction yet). */
                    if (group_run == 0u || group_run > (size_t)secondary) {
                        fail(out, ROBO_I18N_PARSE_ERR_INVALID_GROUPING, at);
                        return ROBO_PARSE_ERROR;
                    }
                } else {
                    if (group_run != (size_t)secondary) {
                        /* en primary=3 secondary=3; hi primary=3 secondary=2 */
                        fail(out, ROBO_I18N_PARSE_ERR_INVALID_GROUPING, at);
                        return ROBO_PARSE_ERROR;
                    }
                }
            }
            saw_group = 1;
            group_run = 0u;
            at = sep_next;
            continue;
        }

        break;
    }

    if (!saw_digit) {
        fail(out, ROBO_I18N_PARSE_ERR_INVALID_DIGIT, at);
        return ROBO_PARSE_ERROR;
    }

    if (saw_group && strict && frac_start == (size_t)-1) {
        /* Final group (least significant) must be primary size. */
        if (group_run != (size_t)primary) {
            fail(out, ROBO_I18N_PARSE_ERR_INVALID_GROUPING, at);
            return ROBO_PARSE_ERROR;
        }
    }

    memset(&out->value, 0, sizeof(out->value));
    out->value.kind = ROBO_I18N_DECIMAL_NUMBER;
    if (frac_start == (size_t)-1) {
        out->value.scale = 0u;
        out->value.digit_count = (uint8_t)dn;
        memcpy(out->value.digits, digs, dn);
    } else {
        size_t frac = dn - frac_start;
        if (frac > 255u) {
            fail(out, ROBO_I18N_PARSE_ERR_OVERFLOW, at);
            return ROBO_PARSE_ERROR;
        }
        out->value.scale = (uint8_t)frac;
        out->value.digit_count = (uint8_t)dn;
        memcpy(out->value.digits, digs, dn);
    }

    /* Strip leading integer zeros but keep at least one digit before scale. */
    {
        size_t int_len = (size_t)out->value.digit_count - (size_t)out->value.scale;
        size_t lead = 0u;
        while (lead + 1u < int_len && out->value.digits[lead] == '0') {
            lead++;
        }
        if (lead > 0u) {
            size_t rest = (size_t)out->value.digit_count - lead;
            memmove(out->value.digits, out->value.digits + lead, rest);
            out->value.digit_count = (uint8_t)rest;
        }
    }

    *io_at = at;
    return ROBO_OK;
}

static robo_result_t parse_number_core(const robo_i18n_number_parser_t *parser,
                                       const char *text,
                                       size_t len,
                                       robo_i18n_parse_result_t *out)
{
    size_t at = 0u;
    int negative = 0;
    robo_result_t r;
    size_t next = 0u;

    robo_i18n_parse_result_clear(out);

    if (!parser || !parser->ready || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!text && len > 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (len > ROBO_I18N_PARSE_INPUT_MAX) {
        fail(out, ROBO_I18N_PARSE_ERR_INPUT_TOO_LONG, 0u);
        return ROBO_INVALID_ARGUMENT;
    }
    if (len == 0u) {
        fail(out, ROBO_I18N_PARSE_ERR_EMPTY, 0u);
        return ROBO_PARSE_ERROR;
    }
    if (!robo_unicode_utf8_validate(text, len)) {
        fail(out, ROBO_I18N_PARSE_ERR_INVALID_UTF8, 0u);
        return ROBO_PARSE_ERROR;
    }

    if (parser->options.allow_leading_ws) {
        at = skip_ws(text, len, at);
    }
    if (at >= len) {
        fail(out, ROBO_I18N_PARSE_ERR_EMPTY, at);
        return ROBO_PARSE_ERROR;
    }

    /* Specials: NaN / Infinity */
    if (prefix_eq(text, len, at, parser->symbols.nan_symbol)) {
        size_t n = strlen(parser->symbols.nan_symbol);
        out->value.kind = ROBO_I18N_DECIMAL_NAN;
        at += n;
        goto finish;
    }
    if (prefix_eq(text, len, at, parser->symbols.infinity_symbol)) {
        size_t n = strlen(parser->symbols.infinity_symbol);
        out->value.kind = ROBO_I18N_DECIMAL_POS_INFINITY;
        at += n;
        goto finish;
    }

    /* Sign */
    if (sep_eq(parser->symbols.minus_sign, text, len, at, &next)) {
        negative = 1;
        at = next;
    } else if (text[at] == '-') {
        negative = 1;
        at += 1u;
    } else if (sep_eq(parser->symbols.plus_sign, text, len, at, &next)) {
        at = next;
    } else if (text[at] == '+') {
        at += 1u;
    }

    if (prefix_eq(text, len, at, parser->symbols.infinity_symbol)) {
        size_t n = strlen(parser->symbols.infinity_symbol);
        out->value.kind =
            negative ? ROBO_I18N_DECIMAL_NEG_INFINITY : ROBO_I18N_DECIMAL_POS_INFINITY;
        at += n;
        goto finish;
    }

    r = parse_number_body(parser, text, len, &at, out,
                          parser->options.integer_only ? 0 : 1);
    if (r != ROBO_OK) {
        return r;
    }
    if (out->value.kind == ROBO_I18N_DECIMAL_NUMBER) {
        out->value.negative = (uint8_t)negative;
    }

finish:
    out->consumed = at;
    if (parser->options.allow_trailing_ws) {
        at = skip_ws(text, len, at);
    }
    if (parser->options.require_full && at < len) {
        fail(out, ROBO_I18N_PARSE_ERR_TRAILING, at);
        return ROBO_PARSE_ERROR;
    }
    out->consumed = at;
    out->error = ROBO_I18N_PARSE_ERR_NONE;
    out->error_offset = at;
    return ROBO_OK;
}

robo_result_t robo_i18n_parse_number(const robo_i18n_number_parser_t *parser,
                                     const char *text,
                                     size_t len,
                                     robo_i18n_parse_result_t *out)
{
    return parse_number_core(parser, text, len, out);
}

robo_result_t robo_i18n_parse_integer(const robo_i18n_number_parser_t *parser,
                                      const char *text,
                                      size_t len,
                                      robo_i18n_parse_result_t *out)
{
    robo_i18n_number_parser_t tmp;
    if (!parser || !parser->ready) {
        return ROBO_INVALID_ARGUMENT;
    }
    tmp = *parser;
    tmp.options.integer_only = 1;
    return parse_number_core(&tmp, text, len, out);
}

/* ---- currency ---- */

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

static int code_in_list(const char list[][4], uint8_t count, const char *code)
{
    uint8_t i;
    for (i = 0u; i < count; i++) {
        if (strcmp(list[i], code) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_ambig(robo_i18n_parse_result_t *out, const char *code)
{
    uint8_t i;
    if (!out || !code || strlen(code) != 3u) {
        return;
    }
    for (i = 0u; i < out->ambiguous_count; i++) {
        if (strcmp(out->ambiguous[i], code) == 0) {
            return;
        }
    }
    if (out->ambiguous_count >= ROBO_I18N_PARSE_AMBIG_MAX) {
        return;
    }
    memcpy(out->ambiguous[out->ambiguous_count], code, 4u);
    out->ambiguous_count++;
}

typedef struct curr_match {
    char code[4];
    size_t len; /* matched byte length */
} curr_match_t;

#define CURR_MATCH_MAX 32

static void consider_match(curr_match_t *best,
                           size_t *best_n,
                           size_t *best_len,
                           const char *code,
                           size_t mlen)
{
    size_t i;
    if (!code || strlen(code) != 3u || mlen == 0u) {
        return;
    }
    if (mlen > *best_len) {
        *best_len = mlen;
        *best_n = 0u;
    }
    if (mlen < *best_len) {
        return;
    }
    for (i = 0u; i < *best_n; i++) {
        if (strcmp(best[i].code, code) == 0) {
            return;
        }
    }
    if (*best_n >= CURR_MATCH_MAX) {
        return;
    }
    memcpy(best[*best_n].code, code, 4u);
    best[*best_n].len = mlen;
    (*best_n)++;
}

static void collect_symbol_matches(const robo_i18n_locale_t *locale,
                                   const char *text,
                                   size_t len,
                                   size_t at,
                                   int allow_names,
                                   curr_match_t *best,
                                   size_t *best_n,
                                   size_t *best_len)
{
    robo_i18n_locale_t cur = *locale;
    int guard = 0;

    cur.extensions[0] = '\0';
    cur.privateuse[0] = '\0';
    cur.variant_count = 0;

    while (guard++ < 16) {
        char tag[ROBO_I18N_TAG_MAX];
        uint32_t i;
        format_tag_simple(&cur, tag, sizeof(tag));
        for (i = 0u; i < robo_disp_currency_count; i++) {
            const robo_disp_currency_row_t *row = &robo_disp_currencies[i];
            const char *loc = robo_disp_string(row->display_locale);
            const char *code;
            const char *sym;
            const char *nar;
            const char *name;
            if (!loc || strcmp(loc, tag) != 0) {
                continue;
            }
            code = robo_disp_string(row->code);
            if (!code) {
                continue;
            }
            sym = robo_disp_string(row->symbol);
            nar = robo_disp_string(row->narrow_symbol);
            name = allow_names ? robo_disp_string(row->name) : NULL;
            if (sym && sym[0] && prefix_eq(text, len, at, sym)) {
                consider_match(best, best_n, best_len, code, strlen(sym));
            }
            if (nar && nar[0] && prefix_eq(text, len, at, nar)) {
                consider_match(best, best_n, best_len, code, strlen(nar));
            }
            if (name && name[0] && prefix_eq(text, len, at, name)) {
                consider_match(best, best_n, best_len, code, strlen(name));
            }
        }
        if (robo_i18n_locale_data_parent(&cur, &cur) != ROBO_OK) {
            break;
        }
        if (strcmp(cur.language, "root") == 0) {
            break;
        }
    }
}

static int match_iso_code(const char *text, size_t len, size_t at, char out[4], size_t *next)
{
    size_t i;
    if (at + 3u > len) {
        return 0;
    }
    for (i = 0u; i < 3u; i++) {
        if (!ascii_is_alpha((unsigned char)text[at + i])) {
            return 0;
        }
        out[i] = ascii_upper(text[at + i]);
    }
    out[3] = '\0';
    /* Require boundary: not followed by another letter */
    if (at + 3u < len && ascii_is_alpha((unsigned char)text[at + 3u])) {
        return 0;
    }
    *next = at + 3u;
    return 1;
}

/**
 * Match a currency token at @p at. Returns:
 *   ROBO_OK — unique match in out->currency, *io_at advanced
 *   ROBO_PARSE_ERROR with AMBIGUOUS — candidates filled
 *   ROBO_NOT_FOUND — no match
 */
static robo_result_t match_currency_token(const robo_i18n_currency_parser_t *parser,
                                          const char *text,
                                          size_t len,
                                          size_t *io_at,
                                          robo_i18n_parse_result_t *out)
{
    size_t at = *io_at;
    char iso[4];
    size_t next = at;
    curr_match_t best[CURR_MATCH_MAX];
    size_t best_n = 0u;
    size_t best_len = 0u;
    size_t i;

    out->currency_ambiguous = 0u;
    out->ambiguous_count = 0u;

    if (match_iso_code(text, len, at, iso, &next)) {
        memcpy(out->currency, iso, 4u);
        *io_at = next;
        return ROBO_OK;
    }

    collect_symbol_matches(&parser->number.locale, text, len, at,
                           parser->options.allow_currency_names, best, &best_n,
                           &best_len);
    if (best_n == 0u) {
        return ROBO_NOT_FOUND;
    }

    /* Filter by accepted set if provided */
    if (parser->options.accepted_count > 0u) {
        curr_match_t filtered[CURR_MATCH_MAX];
        size_t fn = 0u;
        for (i = 0u; i < best_n; i++) {
            if (code_in_list(parser->options.accepted, parser->options.accepted_count,
                             best[i].code)) {
                filtered[fn++] = best[i];
            }
        }
        if (fn == 0u) {
            fail(out, ROBO_I18N_PARSE_ERR_INVALID_CURRENCY, at);
            return ROBO_PARSE_ERROR;
        }
        memcpy(best, filtered, fn * sizeof(best[0]));
        best_n = fn;
    }

    if (parser->options.require_currency[0]) {
        int found = 0;
        for (i = 0u; i < best_n; i++) {
            if (strcmp(best[i].code, parser->options.require_currency) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            fail(out, ROBO_I18N_PARSE_ERR_INVALID_CURRENCY, at);
            return ROBO_PARSE_ERROR;
        }
        memcpy(out->currency, parser->options.require_currency, 4u);
        *io_at = at + best_len;
        return ROBO_OK;
    }

    if (best_n == 1u) {
        memcpy(out->currency, best[0].code, 4u);
        *io_at = at + best_len;
        return ROBO_OK;
    }

    /* Ambiguous among best[] — prefer locale default in permissive mode. */
    if (parser->number.options.mode == ROBO_I18N_PARSE_PERMISSIVE &&
        parser->locale_default[0]) {
        for (i = 0u; i < best_n; i++) {
            if (strcmp(best[i].code, parser->locale_default) == 0) {
                memcpy(out->currency, parser->locale_default, 4u);
                out->currency_ambiguous = 0u;
                out->ambiguous_count = 0u;
                *io_at = at + best_len;
                return ROBO_OK;
            }
        }
    }

    for (i = 0u; i < best_n; i++) {
        add_ambig(out, best[i].code);
    }
    out->currency_ambiguous = 1u;

    fail(out, ROBO_I18N_PARSE_ERR_AMBIGUOUS_CURRENCY, at);
    return ROBO_PARSE_ERROR;
}

static size_t skip_currency_space(const char *text, size_t len, size_t at)
{
    /* NBSP, thin space, regular space */
    while (at < len) {
        robo_uchar32_t cp = 0;
        size_t next = at;
        if (robo_unicode_utf8_next(text, len, at, ROBO_UNICODE_STRICT, &cp, &next) !=
            ROBO_UNICODE_OK) {
            break;
        }
        if (cp == 0x20u || cp == 0xA0u || cp == 0x202Fu || cp == 0x2009u ||
            robo_unicode_is_whitespace(cp)) {
            at = next;
            continue;
        }
        break;
    }
    return at;
}

robo_result_t robo_i18n_currency_parser_init(
    robo_i18n_currency_parser_t *parser,
    const robo_i18n_locale_t *locale,
    const robo_i18n_currency_parse_options_t *opts)
{
    robo_result_t r;

    if (!parser || !locale) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(parser, 0, sizeof(*parser));
    if (opts) {
        parser->options = *opts;
    } else {
        robo_i18n_currency_parse_options_init(&parser->options);
    }
    r = robo_i18n_number_parser_init(&parser->number, locale, &parser->options.number);
    if (r != ROBO_OK) {
        return r;
    }
    if (robo_i18n_currency_for_locale(locale, parser->locale_default) != ROBO_OK) {
        parser->locale_default[0] = '\0';
    }
    if (parser->options.require_currency[0]) {
        char norm[4];
        if (robo_i18n_currency_normalize(parser->options.require_currency, 3u, norm) !=
            ROBO_OK) {
            robo_i18n_number_parser_destroy(&parser->number);
            return ROBO_INVALID_ARGUMENT;
        }
        memcpy(parser->options.require_currency, norm, 4u);
    }
    parser->ready = 1u;
    return ROBO_OK;
}

void robo_i18n_currency_parser_destroy(robo_i18n_currency_parser_t *parser)
{
    if (!parser) {
        return;
    }
    robo_i18n_number_parser_destroy(&parser->number);
    memset(parser, 0, sizeof(*parser));
}

static robo_result_t finalize_currency(const robo_i18n_currency_parser_t *parser,
                                       robo_i18n_parse_result_t *out)
{
    if (parser->options.require_currency[0]) {
        if (!out->currency[0]) {
            memcpy(out->currency, parser->options.require_currency, 4u);
        } else if (strcmp(out->currency, parser->options.require_currency) != 0) {
            fail(out, ROBO_I18N_PARSE_ERR_INVALID_CURRENCY, out->consumed);
            return ROBO_PARSE_ERROR;
        }
    } else if (!out->currency[0] && parser->locale_default[0]) {
        memcpy(out->currency, parser->locale_default, 4u);
    }
    if (parser->options.accepted_count > 0u && out->currency[0]) {
        if (!code_in_list(parser->options.accepted, parser->options.accepted_count,
                          out->currency)) {
            fail(out, ROBO_I18N_PARSE_ERR_INVALID_CURRENCY, out->consumed);
            return ROBO_PARSE_ERROR;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_parse_currency(const robo_i18n_currency_parser_t *parser,
                                       const char *text,
                                       size_t len,
                                       robo_i18n_parse_result_t *out)
{
    size_t at = 0u;
    int accounting = 0;
    robo_result_t r;
    robo_i18n_parse_result_t num;
    size_t save;
    char curr[4];
    int have_curr = 0;

    robo_i18n_parse_result_clear(out);
    if (!parser || !parser->ready || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!text && len > 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (len > ROBO_I18N_PARSE_INPUT_MAX) {
        fail(out, ROBO_I18N_PARSE_ERR_INPUT_TOO_LONG, 0u);
        return ROBO_INVALID_ARGUMENT;
    }
    if (len == 0u) {
        fail(out, ROBO_I18N_PARSE_ERR_EMPTY, 0u);
        return ROBO_PARSE_ERROR;
    }
    if (!robo_unicode_utf8_validate(text, len)) {
        fail(out, ROBO_I18N_PARSE_ERR_INVALID_UTF8, 0u);
        return ROBO_PARSE_ERROR;
    }

    if (parser->options.number.allow_leading_ws) {
        at = skip_ws(text, len, at);
    }
    if (at >= len) {
        fail(out, ROBO_I18N_PARSE_ERR_EMPTY, at);
        return ROBO_PARSE_ERROR;
    }

    if (text[at] == '(') {
        accounting = 1;
        at++;
        at = skip_currency_space(text, len, at);
    }

    curr[0] = '\0';
    save = at;

    /* Attempt currency-then-number */
    {
        size_t cat = at;
        robo_i18n_parse_result_t tok;
        robo_i18n_parse_result_clear(&tok);
        r = match_currency_token(parser, text, len, &cat, &tok);
        if (r == ROBO_OK) {
            size_t nat = skip_currency_space(text, len, cat);
            robo_i18n_number_parser_t np = parser->number;
            np.options.require_full = 0;
            np.options.allow_leading_ws = 0;
            np.options.allow_trailing_ws = 0;
            r = parse_number_core(&np, text + nat, len - nat, &num);
            if (r == ROBO_OK) {
                memcpy(curr, tok.currency, 4u);
                have_curr = 1;
                at = nat + num.consumed;
                *out = num;
                memcpy(out->currency, curr, 4u);
                goto after_number;
            }
        } else if (r == ROBO_PARSE_ERROR &&
                   tok.error == ROBO_I18N_PARSE_ERR_AMBIGUOUS_CURRENCY) {
            *out = tok;
            return ROBO_PARSE_ERROR;
        }
    }

    /* Number-then-currency */
    at = save;
    {
        robo_i18n_number_parser_t np = parser->number;
        int neg_from_accounting = accounting;
        np.options.require_full = 0;
        np.options.allow_leading_ws = 0;
        np.options.allow_trailing_ws = 0;
        r = parse_number_core(&np, text + at, len - at, &num);
        if (r != ROBO_OK) {
            *out = num;
            out->error_offset += at;
            return r;
        }
        *out = num;
        if (neg_from_accounting && out->value.kind == ROBO_I18N_DECIMAL_NUMBER) {
            out->value.negative = 1u;
        }
        at = at + num.consumed;
        at = skip_currency_space(text, len, at);
        if (at < len) {
            size_t cat = at;
            robo_i18n_parse_result_t tok;
            robo_i18n_parse_result_clear(&tok);
            r = match_currency_token(parser, text, len, &cat, &tok);
            if (r == ROBO_OK) {
                memcpy(out->currency, tok.currency, 4u);
                have_curr = 1;
                at = cat;
            } else if (r == ROBO_PARSE_ERROR) {
                *out = tok;
                return ROBO_PARSE_ERROR;
            }
        }
    }

after_number:
    if (accounting) {
        at = skip_currency_space(text, len, at);
        if (at >= len || text[at] != ')') {
            fail(out, ROBO_I18N_PARSE_ERR_UNEXPECTED_CHAR, at);
            return ROBO_PARSE_ERROR;
        }
        at++;
        if (out->value.kind == ROBO_I18N_DECIMAL_NUMBER) {
            out->value.negative = 1u;
        }
    }

    if (!have_curr) {
        out->currency[0] = '\0';
    }

    r = finalize_currency(parser, out);
    if (r != ROBO_OK) {
        return r;
    }

    out->consumed = at;
    if (parser->options.number.allow_trailing_ws) {
        at = skip_ws(text, len, at);
    }
    if (parser->options.number.require_full && at < len) {
        fail(out, ROBO_I18N_PARSE_ERR_TRAILING, at);
        return ROBO_PARSE_ERROR;
    }
    out->consumed = at;
    out->error = ROBO_I18N_PARSE_ERR_NONE;
    out->error_offset = at;
    return ROBO_OK;
}
