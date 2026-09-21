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

static void CopyId(char *dst, size_t cap, const char *src)
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

static char AsciiUpper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static int AsciiIsAlpha(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

void I18nParseOptionsInit(I18nParseOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->mode = RoboI18nParseStrict;
    opts->allowLeadingWs = 1;
    opts->allowTrailingWs = 1;
    opts->requireFull = 1;
    opts->integerOnly = 0;
}

void I18nParseResultClear(I18nParseResult *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

void I18nCurrencyParseOptionsInit(I18nCurrencyParseOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    I18nParseOptionsInit(&opts->number);
    opts->allowCurrencyNames = 1;
}

const char *I18nParseErrString(I18nParseErr err)
{
    switch (err) {
    case RoboI18nParseErrNone:
        return "ok";
    case RoboI18nParseErrEmpty:
        return "empty input";
    case RoboI18nParseErrInvalidUtf8:
        return "invalid UTF-8";
    case RoboI18nParseErrInvalidDigit:
        return "invalid digit";
    case RoboI18nParseErrInvalidGrouping:
        return "invalid grouping";
    case RoboI18nParseErrInvalidDecimal:
        return "invalid decimal separator";
    case RoboI18nParseErrInvalidSign:
        return "invalid sign";
    case RoboI18nParseErrInvalidCurrency:
        return "invalid currency";
    case RoboI18nParseErrAmbiguousCurrency:
        return "ambiguous currency";
    case RoboI18nParseErrUnexpectedChar:
        return "unexpected character";
    case RoboI18nParseErrTrailing:
        return "trailing input";
    case RoboI18nParseErrOverflow:
        return "overflow";
    case RoboI18nParseErrUnsupportedLocale:
        return "unsupported locale";
    case RoboI18nParseErrInputTooLong:
        return "input too long";
    default:
        return "unknown parse error";
    }
}

static void fail(I18nParseResult *out,
                 I18nParseErr err,
                 size_t offset)
{
    if (!out) {
        return;
    }
    out->error = err;
    out->errorOffset = offset;
}

static int PrefixEq(const char *text, size_t len, size_t at, const char *lit)
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

static size_t SkipWs(const char *text, size_t len, size_t at)
{
    while (at < len) {
        Uchar32 cp = 0;
        size_t next = at;
        if (UnicodeUtf8Next(text, len, at, RoboUnicodeStrict, &cp, &next) !=
            RoboUnicodeOk) {
            break;
        }
        if (!UnicodeIsWhitespace(cp)) {
            break;
        }
        at = next;
    }
    return at;
}

static int MatchDigit(const I18nNumberParser *p,
                       const char *text,
                       size_t len,
                       size_t at,
                       int *outD,
                       size_t *outNext)
{
    int d;
    if (at >= len) {
        return 0;
    }
    for (d = 0; d < 10; d++) {
        if (p->digitLen[d] == 0u) {
            continue;
        }
        if (at + p->digitLen[d] <= len &&
            memcmp(text + at, p->digitUtf8[d], p->digitLen[d]) == 0) {
            *outD = d;
            *outNext = at + p->digitLen[d];
            return 1;
        }
    }
    /* Always accept ASCII digits as a fallback for mixed input in permissive,
     * and when numbering system is latn. */
    if (text[at] >= '0' && text[at] <= '9') {
        *outD = (int)(text[at] - '0');
        *outNext = at + 1u;
        return 1;
    }
    return 0;
}

static int BuildDigitTable(I18nNumberParser *parser)
{
    const char *g;
    size_t glen;
    size_t off = 0u;
    int d;

    for (d = 0; d < 10; d++) {
        parser->digitUtf8[d] = NULL;
        parser->digitLen[d] = 0u;
    }
    g = parser->symbols.digitGlyphs;
    if (!g || !g[0]) {
        static const char ascii[] = "0123456789";
        for (d = 0; d < 10; d++) {
            parser->digitUtf8[d] = ascii + d;
            parser->digitLen[d] = 1u;
        }
        return 1;
    }
    glen = strlen(g);
    for (d = 0; d < 10; d++) {
        Uchar32 cp = 0;
        size_t next = off;
        if (off >= glen) {
            return 0;
        }
        if (UnicodeUtf8Next(g, glen, off, RoboUnicodeStrict, &cp, &next) !=
            RoboUnicodeOk) {
            return 0;
        }
        parser->digitUtf8[d] = g + off;
        parser->digitLen[d] = (uint8_t)(next - off);
        off = next;
    }
    return 1;
}

Result I18nNumberParserInit(I18nNumberParser *parser,
                                           const I18nLocale *locale,
                                           const I18nParseOptions *opts)
{
    I18nNumberOptions nopts;
    Result r;

    if (!parser || !locale) {
        return RoboInvalidArgument;
    }
    memset(parser, 0, sizeof(*parser));
    if (opts) {
        parser->options = *opts;
    } else {
        I18nParseOptionsInit(&parser->options);
    }
    parser->locale = *locale;

    I18nNumberOptionsInit(&nopts);
    CopyId(nopts.numberingSystem, sizeof(nopts.numberingSystem),
            parser->options.numberingSystem);
    r = I18nNumberFormatterInit(&parser->symbols, locale, &nopts);
    if (r != RoboOk) {
        return r == RoboNotFound ? RoboNotFound : r;
    }
    if (!BuildDigitTable(parser)) {
        I18nNumberFormatterDestroy(&parser->symbols);
        return RoboError;
    }
    parser->ready = 1u;
    return RoboOk;
}

void I18nNumberParserDestroy(I18nNumberParser *parser)
{
    if (!parser) {
        return;
    }
    I18nNumberFormatterDestroy(&parser->symbols);
    memset(parser, 0, sizeof(*parser));
}

static int SepEq(const char *sep, const char *text, size_t len, size_t at, size_t *next)
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
 * Updates *ioAt on success. Grouping validated per mode.
 */
static Result ParseNumberBody(const I18nNumberParser *parser,
                                       const char *text,
                                       size_t len,
                                       size_t *ioAt,
                                       I18nParseResult *out,
                                       int allowFraction)
{
    size_t at = *ioAt;
    char digs[RoboI18nDecimalDigitsMax];
    size_t dn = 0u;
    size_t fracStart = (size_t)-1;
    int sawDigit = 0;
    int sawGroup = 0;
    size_t groupRun = 0u; /* digits since last group / start */
    uint8_t primary = parser->symbols.primaryGroup;
    uint8_t secondary = parser->symbols.secondaryGroup;
    int strict = (parser->options.mode == RoboI18nParseStrict);
    const char *dec = parser->symbols.decimalSep;
    const char *grp = parser->symbols.groupSep;

    if (primary == 0u) {
        primary = 3u;
    }
    if (secondary == 0u) {
        secondary = primary;
    }

    while (at < len) {
        int d = 0;
        size_t next = at;
        size_t sepNext = at;

        if (MatchDigit(parser, text, len, at, &d, &next)) {
            if (dn >= RoboI18nDecimalDigitsMax) {
                fail(out, RoboI18nParseErrOverflow, at);
                return RoboParseError;
            }
            digs[dn++] = (char)('0' + d);
            sawDigit = 1;
            groupRun++;
            at = next;
            continue;
        }

        if (allowFraction && fracStart == (size_t)-1 &&
            SepEq(dec, text, len, at, &sepNext)) {
            /* Decimal separator must not appear before any digit in strict
             * unless we allow ".5" in permissive. */
            if (!sawDigit) {
                if (strict) {
                    fail(out, RoboI18nParseErrInvalidDecimal, at);
                    return RoboParseError;
                }
                digs[dn++] = '0';
                sawDigit = 1;
            }
            if (sawGroup && strict) {
                /* Last group before decimal must be primary size. */
                if (groupRun != (size_t)primary) {
                    fail(out, RoboI18nParseErrInvalidGrouping, at);
                    return RoboParseError;
                }
            }
            fracStart = dn;
            groupRun = 0u;
            at = sepNext;
            continue;
        }

        if (fracStart == (size_t)-1 && SepEq(grp, text, len, at, &sepNext)) {
            if (!sawDigit) {
                fail(out, RoboI18nParseErrInvalidGrouping, at);
                return RoboParseError;
            }
            if (strict) {
                if (!sawGroup) {
                    /* First (most-significant) group: 1..secondary (or primary
                     * when no secondary distinction yet). */
                    if (groupRun == 0u || groupRun > (size_t)secondary) {
                        fail(out, RoboI18nParseErrInvalidGrouping, at);
                        return RoboParseError;
                    }
                } else {
                    if (groupRun != (size_t)secondary) {
                        /* en primary=3 secondary=3; hi primary=3 secondary=2 */
                        fail(out, RoboI18nParseErrInvalidGrouping, at);
                        return RoboParseError;
                    }
                }
            }
            sawGroup = 1;
            groupRun = 0u;
            at = sepNext;
            continue;
        }

        break;
    }

    if (!sawDigit) {
        fail(out, RoboI18nParseErrInvalidDigit, at);
        return RoboParseError;
    }

    if (sawGroup && strict && fracStart == (size_t)-1) {
        /* Final group (least significant) must be primary size. */
        if (groupRun != (size_t)primary) {
            fail(out, RoboI18nParseErrInvalidGrouping, at);
            return RoboParseError;
        }
    }

    memset(&out->value, 0, sizeof(out->value));
    out->value.kind = RoboI18nDecimalNumber;
    if (fracStart == (size_t)-1) {
        out->value.scale = 0u;
        out->value.digitCount = (uint8_t)dn;
        memcpy(out->value.digits, digs, dn);
    } else {
        size_t frac = dn - fracStart;
        if (frac > 255u) {
            fail(out, RoboI18nParseErrOverflow, at);
            return RoboParseError;
        }
        out->value.scale = (uint8_t)frac;
        out->value.digitCount = (uint8_t)dn;
        memcpy(out->value.digits, digs, dn);
    }

    /* Strip leading integer zeros but keep at least one digit before scale. */
    {
        size_t intLen = (size_t)out->value.digitCount - (size_t)out->value.scale;
        size_t lead = 0u;
        while (lead + 1u < intLen && out->value.digits[lead] == '0') {
            lead++;
        }
        if (lead > 0u) {
            size_t rest = (size_t)out->value.digitCount - lead;
            memmove(out->value.digits, out->value.digits + lead, rest);
            out->value.digitCount = (uint8_t)rest;
        }
    }

    *ioAt = at;
    return RoboOk;
}

static Result ParseNumberCore(const I18nNumberParser *parser,
                                       const char *text,
                                       size_t len,
                                       I18nParseResult *out)
{
    size_t at = 0u;
    int negative = 0;
    Result r;
    size_t next = 0u;

    I18nParseResultClear(out);

    if (!parser || !parser->ready || !out) {
        return RoboInvalidArgument;
    }
    if (!text && len > 0u) {
        return RoboInvalidArgument;
    }
    if (len > RoboI18nParseInputMax) {
        fail(out, RoboI18nParseErrInputTooLong, 0u);
        return RoboInvalidArgument;
    }
    if (len == 0u) {
        fail(out, RoboI18nParseErrEmpty, 0u);
        return RoboParseError;
    }
    if (!UnicodeUtf8Validate(text, len)) {
        fail(out, RoboI18nParseErrInvalidUtf8, 0u);
        return RoboParseError;
    }

    if (parser->options.allowLeadingWs) {
        at = SkipWs(text, len, at);
    }
    if (at >= len) {
        fail(out, RoboI18nParseErrEmpty, at);
        return RoboParseError;
    }

    /* Specials: NaN / Infinity */
    if (PrefixEq(text, len, at, parser->symbols.nanSymbol)) {
        size_t n = strlen(parser->symbols.nanSymbol);
        out->value.kind = RoboI18nDecimalNan;
        at += n;
        goto finish;
    }
    if (PrefixEq(text, len, at, parser->symbols.infinitySymbol)) {
        size_t n = strlen(parser->symbols.infinitySymbol);
        out->value.kind = RoboI18nDecimalPosInfinity;
        at += n;
        goto finish;
    }

    /* Sign */
    if (SepEq(parser->symbols.minusSign, text, len, at, &next)) {
        negative = 1;
        at = next;
    } else if (text[at] == '-') {
        negative = 1;
        at += 1u;
    } else if (SepEq(parser->symbols.plusSign, text, len, at, &next)) {
        at = next;
    } else if (text[at] == '+') {
        at += 1u;
    }

    if (PrefixEq(text, len, at, parser->symbols.infinitySymbol)) {
        size_t n = strlen(parser->symbols.infinitySymbol);
        out->value.kind =
            negative ? RoboI18nDecimalNegInfinity : RoboI18nDecimalPosInfinity;
        at += n;
        goto finish;
    }

    r = ParseNumberBody(parser, text, len, &at, out,
                          parser->options.integerOnly ? 0 : 1);
    if (r != RoboOk) {
        return r;
    }
    if (out->value.kind == RoboI18nDecimalNumber) {
        out->value.negative = (uint8_t)negative;
    }

finish:
    out->consumed = at;
    if (parser->options.allowTrailingWs) {
        at = SkipWs(text, len, at);
    }
    if (parser->options.requireFull && at < len) {
        fail(out, RoboI18nParseErrTrailing, at);
        return RoboParseError;
    }
    out->consumed = at;
    out->error = RoboI18nParseErrNone;
    out->errorOffset = at;
    return RoboOk;
}

Result I18nParseNumber(const I18nNumberParser *parser,
                                     const char *text,
                                     size_t len,
                                     I18nParseResult *out)
{
    return ParseNumberCore(parser, text, len, out);
}

Result I18nParseInteger(const I18nNumberParser *parser,
                                      const char *text,
                                      size_t len,
                                      I18nParseResult *out)
{
    I18nNumberParser tmp;
    if (!parser || !parser->ready) {
        return RoboInvalidArgument;
    }
    tmp = *parser;
    tmp.options.integerOnly = 1;
    return ParseNumberCore(&tmp, text, len, out);
}

/* ---- currency ---- */

static void FormatTagSimple(const I18nLocale *loc, char *buf, size_t cap)
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

static int CodeInList(const char list[][4], uint8_t count, const char *code)
{
    uint8_t i;
    for (i = 0u; i < count; i++) {
        if (strcmp(list[i], code) == 0) {
            return 1;
        }
    }
    return 0;
}

static void AddAmbig(I18nParseResult *out, const char *code)
{
    uint8_t i;
    if (!out || !code || strlen(code) != 3u) {
        return;
    }
    for (i = 0u; i < out->ambiguousCount; i++) {
        if (strcmp(out->ambiguous[i], code) == 0) {
            return;
        }
    }
    if (out->ambiguousCount >= RoboI18nParseAmbigMax) {
        return;
    }
    memcpy(out->ambiguous[out->ambiguousCount], code, 4u);
    out->ambiguousCount++;
}

typedef struct currMatch {
    char code[4];
    size_t len; /* matched byte length */
} CurrMatch;

#define CURR_MATCH_MAX 32

static void ConsiderMatch(CurrMatch *best,
                           size_t *bestN,
                           size_t *bestLen,
                           const char *code,
                           size_t mlen)
{
    size_t i;
    if (!code || strlen(code) != 3u || mlen == 0u) {
        return;
    }
    if (mlen > *bestLen) {
        *bestLen = mlen;
        *bestN = 0u;
    }
    if (mlen < *bestLen) {
        return;
    }
    for (i = 0u; i < *bestN; i++) {
        if (strcmp(best[i].code, code) == 0) {
            return;
        }
    }
    if (*bestN >= CURR_MATCH_MAX) {
        return;
    }
    memcpy(best[*bestN].code, code, 4u);
    best[*bestN].len = mlen;
    (*bestN)++;
}

static void CollectSymbolMatches(const I18nLocale *locale,
                                   const char *text,
                                   size_t len,
                                   size_t at,
                                   int allowNames,
                                   CurrMatch *best,
                                   size_t *bestN,
                                   size_t *bestLen)
{
    I18nLocale cur = *locale;
    int guard = 0;

    cur.extensions[0] = '\0';
    cur.privateuse[0] = '\0';
    cur.variantCount = 0;

    while (guard++ < 16) {
        char tag[RoboI18nTagMax];
        uint32_t i;
        FormatTagSimple(&cur, tag, sizeof(tag));
        for (i = 0u; i < DispCurrencyCount; i++) {
            const DispCurrencyRow *row = &DispCurrencies[i];
            const char *loc = DispString(row->displayLocale);
            const char *code;
            const char *sym;
            const char *nar;
            const char *name;
            if (!loc || strcmp(loc, tag) != 0) {
                continue;
            }
            code = DispString(row->code);
            if (!code) {
                continue;
            }
            sym = DispString(row->symbol);
            nar = DispString(row->narrowSymbol);
            name = allowNames ? DispString(row->name) : NULL;
            if (sym && sym[0] && PrefixEq(text, len, at, sym)) {
                ConsiderMatch(best, bestN, bestLen, code, strlen(sym));
            }
            if (nar && nar[0] && PrefixEq(text, len, at, nar)) {
                ConsiderMatch(best, bestN, bestLen, code, strlen(nar));
            }
            if (name && name[0] && PrefixEq(text, len, at, name)) {
                ConsiderMatch(best, bestN, bestLen, code, strlen(name));
            }
        }
        if (I18nLocaleDataParent(&cur, &cur) != RoboOk) {
            break;
        }
        if (strcmp(cur.language, "root") == 0) {
            break;
        }
    }
}

static int MatchIsoCode(const char *text, size_t len, size_t at, char out[4], size_t *next)
{
    size_t i;
    if (at + 3u > len) {
        return 0;
    }
    for (i = 0u; i < 3u; i++) {
        if (!AsciiIsAlpha((unsigned char)text[at + i])) {
            return 0;
        }
        out[i] = AsciiUpper(text[at + i]);
    }
    out[3] = '\0';
    /* Require boundary: not followed by another letter */
    if (at + 3u < len && AsciiIsAlpha((unsigned char)text[at + 3u])) {
        return 0;
    }
    *next = at + 3u;
    return 1;
}

/**
 * Match a currency token at @p at. Returns:
 *   RoboOk — unique match in out->currency, *ioAt advanced
 *   RoboParseError with AMBIGUOUS — candidates filled
 *   RoboNotFound — no match
 */
static Result MatchCurrencyToken(const I18nCurrencyParser *parser,
                                          const char *text,
                                          size_t len,
                                          size_t *ioAt,
                                          I18nParseResult *out)
{
    size_t at = *ioAt;
    char iso[4];
    size_t next = at;
    CurrMatch best[CURR_MATCH_MAX];
    size_t bestN = 0u;
    size_t bestLen = 0u;
    size_t i;

    out->currencyAmbiguous = 0u;
    out->ambiguousCount = 0u;

    if (MatchIsoCode(text, len, at, iso, &next)) {
        memcpy(out->currency, iso, 4u);
        *ioAt = next;
        return RoboOk;
    }

    CollectSymbolMatches(&parser->number.locale, text, len, at,
                           parser->options.allowCurrencyNames, best, &bestN,
                           &bestLen);
    if (bestN == 0u) {
        return RoboNotFound;
    }

    /* Filter by accepted set if provided */
    if (parser->options.acceptedCount > 0u) {
        CurrMatch filtered[CURR_MATCH_MAX];
        size_t fn = 0u;
        for (i = 0u; i < bestN; i++) {
            if (CodeInList(parser->options.accepted, parser->options.acceptedCount,
                             best[i].code)) {
                filtered[fn++] = best[i];
            }
        }
        if (fn == 0u) {
            fail(out, RoboI18nParseErrInvalidCurrency, at);
            return RoboParseError;
        }
        memcpy(best, filtered, fn * sizeof(best[0]));
        bestN = fn;
    }

    if (parser->options.requireCurrency[0]) {
        int found = 0;
        for (i = 0u; i < bestN; i++) {
            if (strcmp(best[i].code, parser->options.requireCurrency) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            fail(out, RoboI18nParseErrInvalidCurrency, at);
            return RoboParseError;
        }
        memcpy(out->currency, parser->options.requireCurrency, 4u);
        *ioAt = at + bestLen;
        return RoboOk;
    }

    if (bestN == 1u) {
        memcpy(out->currency, best[0].code, 4u);
        *ioAt = at + bestLen;
        return RoboOk;
    }

    /* Ambiguous among best[] — prefer locale default in permissive mode. */
    if (parser->number.options.mode == RoboI18nParsePermissive &&
        parser->localeDefault[0]) {
        for (i = 0u; i < bestN; i++) {
            if (strcmp(best[i].code, parser->localeDefault) == 0) {
                memcpy(out->currency, parser->localeDefault, 4u);
                out->currencyAmbiguous = 0u;
                out->ambiguousCount = 0u;
                *ioAt = at + bestLen;
                return RoboOk;
            }
        }
    }

    for (i = 0u; i < bestN; i++) {
        AddAmbig(out, best[i].code);
    }
    out->currencyAmbiguous = 1u;

    fail(out, RoboI18nParseErrAmbiguousCurrency, at);
    return RoboParseError;
}

static size_t SkipCurrencySpace(const char *text, size_t len, size_t at)
{
    /* NBSP, thin space, regular space */
    while (at < len) {
        Uchar32 cp = 0;
        size_t next = at;
        if (UnicodeUtf8Next(text, len, at, RoboUnicodeStrict, &cp, &next) !=
            RoboUnicodeOk) {
            break;
        }
        if (cp == 0x20u || cp == 0xA0u || cp == 0x202Fu || cp == 0x2009u ||
            UnicodeIsWhitespace(cp)) {
            at = next;
            continue;
        }
        break;
    }
    return at;
}

Result I18nCurrencyParserInit(
    I18nCurrencyParser *parser,
    const I18nLocale *locale,
    const I18nCurrencyParseOptions *opts)
{
    Result r;

    if (!parser || !locale) {
        return RoboInvalidArgument;
    }
    memset(parser, 0, sizeof(*parser));
    if (opts) {
        parser->options = *opts;
    } else {
        I18nCurrencyParseOptionsInit(&parser->options);
    }
    r = I18nNumberParserInit(&parser->number, locale, &parser->options.number);
    if (r != RoboOk) {
        return r;
    }
    if (I18nCurrencyForLocale(locale, parser->localeDefault) != RoboOk) {
        parser->localeDefault[0] = '\0';
    }
    if (parser->options.requireCurrency[0]) {
        char norm[4];
        if (I18nCurrencyNormalize(parser->options.requireCurrency, 3u, norm) !=
            RoboOk) {
            I18nNumberParserDestroy(&parser->number);
            return RoboInvalidArgument;
        }
        memcpy(parser->options.requireCurrency, norm, 4u);
    }
    parser->ready = 1u;
    return RoboOk;
}

void I18nCurrencyParserDestroy(I18nCurrencyParser *parser)
{
    if (!parser) {
        return;
    }
    I18nNumberParserDestroy(&parser->number);
    memset(parser, 0, sizeof(*parser));
}

static Result FinalizeCurrency(const I18nCurrencyParser *parser,
                                       I18nParseResult *out)
{
    if (parser->options.requireCurrency[0]) {
        if (!out->currency[0]) {
            memcpy(out->currency, parser->options.requireCurrency, 4u);
        } else if (strcmp(out->currency, parser->options.requireCurrency) != 0) {
            fail(out, RoboI18nParseErrInvalidCurrency, out->consumed);
            return RoboParseError;
        }
    } else if (!out->currency[0] && parser->localeDefault[0]) {
        memcpy(out->currency, parser->localeDefault, 4u);
    }
    if (parser->options.acceptedCount > 0u && out->currency[0]) {
        if (!CodeInList(parser->options.accepted, parser->options.acceptedCount,
                          out->currency)) {
            fail(out, RoboI18nParseErrInvalidCurrency, out->consumed);
            return RoboParseError;
        }
    }
    return RoboOk;
}

Result I18nParseCurrency(const I18nCurrencyParser *parser,
                                       const char *text,
                                       size_t len,
                                       I18nParseResult *out)
{
    size_t at = 0u;
    int accounting = 0;
    Result r;
    I18nParseResult num;
    size_t save;
    char curr[4];
    int haveCurr = 0;

    I18nParseResultClear(out);
    if (!parser || !parser->ready || !out) {
        return RoboInvalidArgument;
    }
    if (!text && len > 0u) {
        return RoboInvalidArgument;
    }
    if (len > RoboI18nParseInputMax) {
        fail(out, RoboI18nParseErrInputTooLong, 0u);
        return RoboInvalidArgument;
    }
    if (len == 0u) {
        fail(out, RoboI18nParseErrEmpty, 0u);
        return RoboParseError;
    }
    if (!UnicodeUtf8Validate(text, len)) {
        fail(out, RoboI18nParseErrInvalidUtf8, 0u);
        return RoboParseError;
    }

    if (parser->options.number.allowLeadingWs) {
        at = SkipWs(text, len, at);
    }
    if (at >= len) {
        fail(out, RoboI18nParseErrEmpty, at);
        return RoboParseError;
    }

    if (text[at] == '(') {
        accounting = 1;
        at++;
        at = SkipCurrencySpace(text, len, at);
    }

    curr[0] = '\0';
    save = at;

    /* Attempt currency-then-number */
    {
        size_t cat = at;
        I18nParseResult tok;
        I18nParseResultClear(&tok);
        r = MatchCurrencyToken(parser, text, len, &cat, &tok);
        if (r == RoboOk) {
            size_t nat = SkipCurrencySpace(text, len, cat);
            I18nNumberParser np = parser->number;
            np.options.requireFull = 0;
            np.options.allowLeadingWs = 0;
            np.options.allowTrailingWs = 0;
            r = ParseNumberCore(&np, text + nat, len - nat, &num);
            if (r == RoboOk) {
                memcpy(curr, tok.currency, 4u);
                haveCurr = 1;
                at = nat + num.consumed;
                *out = num;
                memcpy(out->currency, curr, 4u);
                goto afterNumber;
            }
        } else if (r == RoboParseError &&
                   tok.error == RoboI18nParseErrAmbiguousCurrency) {
            *out = tok;
            return RoboParseError;
        }
    }

    /* Number-then-currency */
    at = save;
    {
        I18nNumberParser np = parser->number;
        int negFromAccounting = accounting;
        np.options.requireFull = 0;
        np.options.allowLeadingWs = 0;
        np.options.allowTrailingWs = 0;
        r = ParseNumberCore(&np, text + at, len - at, &num);
        if (r != RoboOk) {
            *out = num;
            out->errorOffset += at;
            return r;
        }
        *out = num;
        if (negFromAccounting && out->value.kind == RoboI18nDecimalNumber) {
            out->value.negative = 1u;
        }
        at = at + num.consumed;
        at = SkipCurrencySpace(text, len, at);
        if (at < len) {
            size_t cat = at;
            I18nParseResult tok;
            I18nParseResultClear(&tok);
            r = MatchCurrencyToken(parser, text, len, &cat, &tok);
            if (r == RoboOk) {
                memcpy(out->currency, tok.currency, 4u);
                haveCurr = 1;
                at = cat;
            } else if (r == RoboParseError) {
                *out = tok;
                return RoboParseError;
            }
        }
    }

afterNumber:
    if (accounting) {
        at = SkipCurrencySpace(text, len, at);
        if (at >= len || text[at] != ')') {
            fail(out, RoboI18nParseErrUnexpectedChar, at);
            return RoboParseError;
        }
        at++;
        if (out->value.kind == RoboI18nDecimalNumber) {
            out->value.negative = 1u;
        }
    }

    if (!haveCurr) {
        out->currency[0] = '\0';
    }

    r = FinalizeCurrency(parser, out);
    if (r != RoboOk) {
        return r;
    }

    out->consumed = at;
    if (parser->options.number.allowTrailingWs) {
        at = SkipWs(text, len, at);
    }
    if (parser->options.number.requireFull && at < len) {
        fail(out, RoboI18nParseErrTrailing, at);
        return RoboParseError;
    }
    out->consumed = at;
    out->error = RoboI18nParseErrNone;
    out->errorOffset = at;
    return RoboOk;
}
