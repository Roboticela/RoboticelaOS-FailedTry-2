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

static int AsciiIsAlpha(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int AsciiIsDigit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static char AsciiUpper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

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

static int ReadKeywordFromOther(const I18nLocale *loc,
                                   const char *key,
                                   char *out,
                                   size_t outCap)
{
    I18nUnicodeKeywords kw;
    const char *p;
    char needle[16];
    size_t klen;

    if (!loc || !key || !out || outCap == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (I18nLocaleGetKeyword(loc, key, out, outCap) == RoboOk && out[0]) {
        return 1;
    }
    if (I18nLocaleUnicodeKeywords(loc, &kw) != RoboOk) {
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
            while (hit[i] && hit[i] != '-' && i + 1u < outCap) {
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

void I18nCurrencyOptionsInit(I18nCurrencyOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->style = RoboI18nCurrencyStyleSymbol;
    opts->usage = RoboI18nCurrencyUsageStandard;
    opts->rounding = RoboI18nRoundHalfEven;
}

Result I18nCurrencyNormalize(const char *code,
                                           size_t codeLen,
                                           char out[4])
{
    size_t i;
    if (!out) {
        return RoboInvalidArgument;
    }
    out[0] = '\0';
    if (!code || codeLen != 3u) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < 3u; i++) {
        unsigned char c = (unsigned char)code[i];
        if (!AsciiIsAlpha(c)) {
            return RoboInvalidArgument;
        }
        out[i] = AsciiUpper((char)c);
    }
    out[3] = '\0';
    return RoboOk;
}

int I18nCurrencyIsWellFormed(const char *code, size_t codeLen)
{
    char tmp[4];
    return I18nCurrencyNormalize(code, codeLen, tmp) == RoboOk;
}

static void FillDefaultInfo(I18nCurrencyInfo *out, const char code[4])
{
    memset(out, 0, sizeof(*out));
    memcpy(out->code, code, 4u);
    out->numeric = -1;
    out->digits = 2u;
    out->rounding = 0u;
    out->cashDigits = -1;
    out->cashRounding = -1;
    out->flags = 0u;
    out->known = 0u;
}

Result I18nCurrencyLookup(const char *code,
                                        size_t codeLen,
                                        I18nCurrencyInfo *out)
{
    char norm[4];
    const CurrMeta *meta;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nCurrencyNormalize(code, codeLen, norm);
    if (r != RoboOk) {
        return r;
    }
    meta = CurrMetaFind(norm, 3u);
    if (!meta) {
        FillDefaultInfo(out, norm);
        return RoboNotFound;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->code, norm, 4u);
    out->numeric = meta->numeric;
    out->digits = meta->digits;
    out->rounding = meta->rounding;
    out->cashDigits = meta->cashDigits;
    out->cashRounding = meta->cashRounding;
    out->flags = meta->flags;
    out->known = 1u;
    return RoboOk;
}

Result I18nCurrencyForRegion(const char *region,
                                            size_t regionLen,
                                            char out[4])
{
    const CurrRegion *row;
    const char *code;
    char reg[8];
    size_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    out[0] = '\0';
    if (!region || regionLen == 0u || regionLen >= sizeof(reg)) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < regionLen; i++) {
        reg[i] = AsciiUpper(region[i]);
    }
    reg[regionLen] = '\0';
    row = CurrRegionFind(reg, regionLen);
    if (!row) {
        return RoboNotFound;
    }
    code = CurrString(row->currency);
    if (!code || strlen(code) != 3u) {
        return RoboNotFound;
    }
    memcpy(out, code, 4u);
    return RoboOk;
}

Result I18nCurrencyForLocale(const I18nLocale *locale,
                                            char out[4])
{
    char cu[16];
    I18nLocale maxed;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    out[0] = '\0';
    if (!locale) {
        return RoboInvalidArgument;
    }
    if (ReadKeywordFromOther(locale, "cu", cu, sizeof(cu))) {
        return I18nCurrencyNormalize(cu, strlen(cu), out);
    }
    if (I18nLocaleMaximize(locale, &maxed) != RoboOk) {
        maxed = *locale;
    }
    if (maxed.region[0] == '\0') {
        return RoboNotFound;
    }
    r = I18nCurrencyForRegion(maxed.region, strlen(maxed.region), out);
    return r;
}

static uint8_t EffectiveDigits(const I18nCurrencyInfo *info,
                                I18nCurrencyUsage usage)
{
    if (usage == RoboI18nCurrencyUsageCash && info->cashDigits >= 0) {
        return (uint8_t)info->cashDigits;
    }
    return info->digits;
}

static int EffectiveRoundingInc(const I18nCurrencyInfo *info,
                                  I18nCurrencyUsage usage)
{
    if (usage == RoboI18nCurrencyUsageCash) {
        if (info->cashRounding >= 0) {
            return (int)info->cashRounding;
        }
        return (int)info->rounding;
    }
    return (int)info->rounding;
}

/**
 * Round decimal to multiple of (inc * 10^-digits) using integer arithmetic.
 * inc<=1 → only truncate/pad via later number options (no extra step).
 */
static Result ApplyIncrementRounding(I18nDecimal *dec,
                                              int digits,
                                              int inc,
                                              I18nRounding mode)
{
    /* Represent value as integer in units of 10^-digits, then round to inc. */
    char tmp[RoboI18nDecimalDigitsMax + 8u];
    size_t intLen;
    size_t fracLen;
    size_t i;
    size_t pos = 0u;
    int64_t units = 0;
    int64_t rounded;
    int64_t q;
    int64_t rem;
    int negative;
    int64_t half;

    if (!dec || dec->kind != RoboI18nDecimalNumber) {
        return RoboOk;
    }
    if (digits < 0 || digits > 18 || inc <= 1) {
        return RoboOk;
    }
    if (dec->digitCount == 0u) {
        return RoboOk;
    }

    negative = dec->negative ? 1 : 0;
    if ((size_t)dec->scale > (size_t)dec->digitCount) {
        return RoboError;
    }
    intLen = (size_t)dec->digitCount - (size_t)dec->scale;
    fracLen = (size_t)dec->scale;

    /* Build digit string with exactly `digits` fraction places (pad/trunc raw). */
    if (intLen == 0u) {
        tmp[pos++] = '0';
    } else {
        if (intLen + (size_t)digits + 1u >= sizeof(tmp)) {
            return RoboNotSupported;
        }
        memcpy(tmp + pos, dec->digits, intLen);
        pos += intLen;
    }
    for (i = 0u; i < (size_t)digits; i++) {
        char ch = '0';
        if (i < fracLen) {
            ch = dec->digits[intLen + i];
        }
        tmp[pos++] = ch;
    }
    tmp[pos] = '\0';

    /* Parse to int64 — reject overflow. */
    for (i = 0u; i < pos; i++) {
        int digit = tmp[i] - '0';
        if (units > (INT64_MAX - digit) / 10) {
            return RoboNotSupported;
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
        case RoboI18nRoundDown:
            bump = 0;
            break;
        case RoboI18nRoundUp:
            bump = 1;
            break;
        case RoboI18nRoundCeiling:
            bump = negative ? 0 : 1;
            break;
        case RoboI18nRoundFloor:
            bump = negative ? 1 : 0;
            break;
        case RoboI18nRoundHalfDown:
            bump = (rem > half) ? 1 : 0;
            break;
        case RoboI18nRoundHalfUp:
            bump = (rem >= half) ? 1 : 0;
            break;
        case RoboI18nRoundHalfEven:
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
                return RoboNotSupported;
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
        if (dn > RoboI18nDecimalDigitsMax) {
            return RoboNotSupported;
        }
        memset(dec, 0, sizeof(*dec));
        dec->kind = RoboI18nDecimalNumber;
        dec->negative = (uint8_t)negative;
        dec->scale = (uint8_t)digits;
        dec->digitCount = (uint8_t)dn;
        memcpy(dec->digits, digs, dn);
    }
    return RoboOk;
}

static int SymbolIsAlphanumeric(const char *sym)
{
    const unsigned char *p;
    if (!sym || !sym[0]) {
        return 0;
    }
    /* Alpha-next-to-number patterns apply when the currency text contains
     * Latin letters/digits (e.g. CHF, Rs). Pure symbols ($ € £ ¥) do not. */
    for (p = (const unsigned char *)sym; *p; p++) {
        if (AsciiIsAlpha(*p) || AsciiIsDigit(*p)) {
            return 1;
        }
    }
    return 0;
}

static const CurrFmt *ResolveFmt(const I18nLocale *locale,
                                          const char *nsys)
{
    I18nLocale cur;
    int guard = 0;

    if (!locale || !nsys || !nsys[0]) {
        return NULL;
    }
    cur = *locale;
    cur.extensions[0] = '\0';
    cur.privateuse[0] = '\0';
    cur.variantCount = 0;

    while (guard++ < 16) {
        char tag[RoboI18nTagMax];
        const CurrFmt *row;
        FormatTagSimple(&cur, tag, sizeof(tag));
        if (tag[0]) {
            row = CurrFmtFind(tag, strlen(tag), nsys, strlen(nsys));
            if (row) {
                return row;
            }
        }
        if (I18nLocaleDataParent(&cur, &cur) != RoboOk) {
            break;
        }
    }
    return CurrFmtFind("und", 3u, nsys, strlen(nsys));
}

static Result AppendBytes(char *buf,
                                  size_t cap,
                                  size_t *pos,
                                  const char *src,
                                  size_t len,
                                  int writing)
{
    if (!pos) {
        return RoboInvalidArgument;
    }
    if (writing) {
        if (!buf) {
            return RoboInvalidArgument;
        }
        if (*pos + len + 1u > cap) {
            *pos += len;
            return RoboError;
        }
        memcpy(buf + *pos, src, len);
    }
    *pos += len;
    return RoboOk;
}

static void FindNumberSpan(const char *pat, size_t len, size_t *start, size_t *end)
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

static size_t CountCurrencySigns(const char *s, size_t len, size_t at)
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

static int IsCurrencySignAt(const char *s, size_t len, size_t at)
{
    return at + 1u < len && (unsigned char)s[at] == 0xC2u &&
           (unsigned char)s[at + 1u] == 0xA4u;
}

static Result ResolveCurrencyText(const I18nCurrencyFormatter *fmt,
                                           const I18nDecimal *value,
                                           char *out,
                                           size_t outCap,
                                           size_t *outLen)
{
    I18nCurrencyNameStyle st;
    Result r;

    if (!fmt || !outLen) {
        return RoboInvalidArgument;
    }
    *outLen = 0u;

    if (fmt->options.style == RoboI18nCurrencyStyleCode) {
        size_t n = 3u;
        if (out && outCap > 0u) {
            if (outCap <= n) {
                *outLen = n;
                return RoboError;
            }
            memcpy(out, fmt->resolvedCurrency, 3u);
            out[3] = '\0';
        }
        *outLen = n;
        return RoboOk;
    }

    if (fmt->options.style == RoboI18nCurrencyStyleName) {
        /* Plural-sensitive name when tables provide it. */
        I18nLocale cur = fmt->locale;
        I18nPluralOperands ops;
        I18nPluralCategory cat = RoboI18nPluralOther;
        int guard = 0;
        const char *name = NULL;

        (void)I18nPluralOperandsFromDecimal(value, &ops);
        (void)I18nPluralSelect(&fmt->locale, RoboI18nPluralCardinal, &ops,
                                      &cat);

        cur.extensions[0] = '\0';
        while (guard++ < 16) {
            char tag[RoboI18nTagMax];
            const CurrPluralName *row;
            FormatTagSimple(&cur, tag, sizeof(tag));
            row = CurrPluralNameFind(tag, strlen(tag), fmt->resolvedCurrency,
                                             3u);
            if (row) {
                int idx = (int)cat;
                int32_t ni = row->names[idx >= 0 && idx < 6 ? idx : 5];
                if (ni < 0) {
                    ni = row->names[5];
                }
                name = CurrString(ni);
                if (name) {
                    break;
                }
            }
            if (I18nLocaleDataParent(&cur, &cur) != RoboOk) {
                break;
            }
        }
        if (name) {
            size_t n = strlen(name);
            if (out && outCap > 0u) {
                if (outCap <= n) {
                    *outLen = n;
                    return RoboError;
                }
                memcpy(out, name, n);
                out[n] = '\0';
            }
            *outLen = n;
            return RoboOk;
        }
        st = RoboI18nCurrencyName;
    } else if (fmt->options.style == RoboI18nCurrencyStyleNarrowSymbol) {
        st = RoboI18nCurrencyNarrowSymbol;
    } else {
        st = RoboI18nCurrencySymbol;
    }

    r = I18nCurrencyDisplayName(fmt->resolvedCurrency, 3u, st, &fmt->locale,
                                        NULL, out, outCap, outLen);
    if (r == RoboNotFound || (r == RoboOk && (!out || *outLen == 0u))) {
        /* Fall back to ISO code. */
        size_t n = 3u;
        if (out && outCap > n) {
            memcpy(out, fmt->resolvedCurrency, 3u);
            out[3] = '\0';
            *outLen = n;
            return RoboOk;
        }
        *outLen = n;
        return out && outCap > 0u ? RoboError : RoboOk;
    }
    return r;
}

static Result ExpandCurrencyPattern(const I18nCurrencyFormatter *fmt,
                                             const char *pattern,
                                             const char *numberUtf8,
                                             size_t numberLen,
                                             const char *currUtf8,
                                             size_t currLen,
                                             int prependMinus,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen)
{
    size_t plen;
    size_t numStart = 0u;
    size_t numEnd = 0u;
    size_t i;
    size_t pos = 0u;
    int writing = (buf != NULL && bufCap > 0u);
    Result r = RoboOk;
    const char *minus = fmt->number.minusSign ? fmt->number.minusSign : "-";

    if (!pattern || !numberUtf8 || !currUtf8 || !outLen) {
        return RoboInvalidArgument;
    }
    *outLen = 0u;
    plen = strlen(pattern);
    FindNumberSpan(pattern, plen, &numStart, &numEnd);

    if (prependMinus) {
        r = AppendBytes(buf, bufCap, &pos, minus, strlen(minus), writing);
        if (r != RoboOk && r != RoboError) {
            return r;
        }
    }

    i = 0u;
    while (i < plen) {
        if (i == numStart && numEnd > numStart) {
            r = AppendBytes(buf, bufCap, &pos, numberUtf8, numberLen, writing);
            if (r != RoboOk && r != RoboError) {
                return r;
            }
            i = numEnd;
            continue;
        }
        if (IsCurrencySignAt(pattern, plen, i)) {
            size_t signs = CountCurrencySigns(pattern, plen, i);
            /* 1 sign = symbol/code already resolved; 2 = ISO; 3 = name — we pass
             * pre-resolved currUtf8 for the active style. */
            (void)signs;
            r = AppendBytes(buf, bufCap, &pos, currUtf8, currLen, writing);
            if (r != RoboOk && r != RoboError) {
                return r;
            }
            i += 2u; /* one ¤ */
            /* Skip extra ¤¤ / ¤¤¤ — already resolved via style */
            while (IsCurrencySignAt(pattern, plen, i)) {
                i += 2u;
            }
            continue;
        }
        r = AppendBytes(buf, bufCap, &pos, pattern + i, 1u, writing);
        if (r != RoboOk && r != RoboError) {
            return r;
        }
        i++;
    }

    if (writing) {
        if (pos + 1u > bufCap) {
            *outLen = pos;
            return RoboError;
        }
        buf[pos] = '\0';
    }
    *outLen = pos;
    return (writing && pos + 1u > bufCap) ? RoboError : RoboOk;
}

static Result ExpandUnitPattern(const char *pattern,
                                         const char *a,
                                         size_t aLen,
                                         const char *b,
                                         size_t bLen,
                                         char *buf,
                                         size_t bufCap,
                                         size_t *outLen)
{
    size_t i;
    size_t plen;
    size_t pos = 0u;
    int writing = (buf != NULL && bufCap > 0u);
    Result r;

    if (!pattern || !a || !b || !outLen) {
        return RoboInvalidArgument;
    }
    plen = strlen(pattern);
    for (i = 0u; i < plen;) {
        if (pattern[i] == '{' && i + 2u < plen && pattern[i + 2u] == '}') {
            if (pattern[i + 1u] == '0') {
                r = AppendBytes(buf, bufCap, &pos, a, aLen, writing);
                if (r != RoboOk && r != RoboError) {
                    return r;
                }
                i += 3u;
                continue;
            }
            if (pattern[i + 1u] == '1') {
                r = AppendBytes(buf, bufCap, &pos, b, bLen, writing);
                if (r != RoboOk && r != RoboError) {
                    return r;
                }
                i += 3u;
                continue;
            }
        }
        r = AppendBytes(buf, bufCap, &pos, pattern + i, 1u, writing);
        if (r != RoboOk && r != RoboError) {
            return r;
        }
        i++;
    }
    if (writing) {
        if (pos + 1u > bufCap) {
            *outLen = pos;
            return RoboError;
        }
        buf[pos] = '\0';
    }
    *outLen = pos;
    return RoboOk;
}

Result I18nCurrencyFormatterInit(
    I18nCurrencyFormatter *fmt,
    const I18nLocale *locale,
    const I18nCurrencyOptions *opts)
{
    I18nNumberOptions nopts;
    I18nCurrencyOptions localOpts;
    char code[4];
    char cf[16];
    Result r;

    if (!fmt || !locale) {
        return RoboInvalidArgument;
    }
    memset(fmt, 0, sizeof(*fmt));
    if (opts) {
        localOpts = *opts;
    } else {
        I18nCurrencyOptionsInit(&localOpts);
    }
    fmt->locale = *locale;
    fmt->options = localOpts;

    if (localOpts.currency[0]) {
        r = I18nCurrencyNormalize(localOpts.currency, strlen(localOpts.currency),
                                         code);
        if (r != RoboOk) {
            return r;
        }
    } else {
        r = I18nCurrencyForLocale(locale, code);
        if (r != RoboOk) {
            /* Last resort: XXX */
            memcpy(code, "XXX", 4u);
        }
    }
    memcpy(fmt->resolvedCurrency, code, 4u);
    r = I18nCurrencyLookup(code, 3u, &fmt->info);
    if (r != RoboOk && r != RoboNotFound) {
        return r;
    }

    fmt->accounting = 0u;
    if (localOpts.style == RoboI18nCurrencyStyleAccounting) {
        fmt->accounting = 1u;
    } else if (ReadKeywordFromOther(locale, "cf", cf, sizeof(cf))) {
        if (strcmp(cf, "account") == 0) {
            fmt->accounting = 1u;
        }
    }

    I18nNumberOptionsInit(&nopts);
    nopts.rounding = localOpts.rounding;
    nopts.useGrouping = 1;
    nopts.minFractionDigits = (int32_t)EffectiveDigits(&fmt->info, localOpts.usage);
    nopts.maxFractionDigits = nopts.minFractionDigits;
    CopyId(nopts.numberingSystem, sizeof(nopts.numberingSystem),
            localOpts.numberingSystem);

    r = I18nNumberFormatterInit(&fmt->number, locale, &nopts);
    if (r != RoboOk) {
        return r;
    }
    fmt->ready = 1u;
    return RoboOk;
}

void I18nCurrencyFormatterDestroy(I18nCurrencyFormatter *fmt)
{
    if (!fmt) {
        return;
    }
    I18nNumberFormatterDestroy(&fmt->number);
    memset(fmt, 0, sizeof(*fmt));
}

const char *I18nCurrencyFormatterCode(const I18nCurrencyFormatter *fmt)
{
    return fmt && fmt->ready ? fmt->resolvedCurrency : NULL;
}

const I18nCurrencyInfo *I18nCurrencyFormatterInfo(
    const I18nCurrencyFormatter *fmt)
{
    return fmt && fmt->ready ? &fmt->info : NULL;
}

Result I18nCurrencyFormat(const I18nCurrencyFormatter *fmt,
                                        const I18nDecimal *value,
                                        char *buf,
                                        size_t bufCap,
                                        size_t *outLen)
{
    I18nDecimal work;
    char numbuf[128];
    char currbuf[192];
    size_t numLen = 0u;
    size_t currLen = 0u;
    size_t want = 0u;
    const CurrFmt *cf;
    const char *pattern;
    const char *patPos;
    const char *semi;
    int negative;
    int prependMinus = 0;
    int digits;
    int inc;
    Result r;
    I18nNumberFormatter nfmt;
    I18nNumberOptions nopts;

    if (!fmt || !fmt->ready || !value || !outLen) {
        return RoboInvalidArgument;
    }
    *outLen = 0u;
    work = *value;

    digits = (int)EffectiveDigits(&fmt->info, fmt->options.usage);
    inc = EffectiveRoundingInc(&fmt->info, fmt->options.usage);
    if (fmt->options.usage == RoboI18nCurrencyUsageCash && inc > 1) {
        r = ApplyIncrementRounding(&work, digits, inc, fmt->options.rounding);
        if (r != RoboOk) {
            return r;
        }
    }

    /* Format magnitude with currency fraction digits. */
    nfmt = fmt->number;
    I18nNumberOptionsInit(&nopts);
    nopts = fmt->number.options;
    nopts.minFractionDigits = digits;
    nopts.maxFractionDigits = digits;
    nopts.showPlus = 0;
    /* Re-init local copy for digit options — reuse resolved symbols from fmt. */
    nfmt.options = nopts;

    negative = (work.kind == RoboI18nDecimalNumber && work.negative) ||
               work.kind == RoboI18nDecimalNegInfinity;
    if (work.kind == RoboI18nDecimalNumber && work.negative) {
        work.negative = 0u; /* pattern / prepend handles sign */
    }

    r = I18nNumberFormat(&nfmt, &work, numbuf, sizeof(numbuf), &numLen);
    if (r != RoboOk) {
        return r;
    }

    r = ResolveCurrencyText(fmt, value, currbuf, sizeof(currbuf), &currLen);
    if (r != RoboOk && r != RoboError) {
        return r;
    }
    if (r == RoboError) {
        return r;
    }

    /* NAME style uses unitPattern, not ¤ pattern. */
    if (fmt->options.style == RoboI18nCurrencyStyleName) {
        I18nPluralOperands ops;
        I18nPluralCategory cat = RoboI18nPluralOther;
        const char *up = "{0} {1}";
        int idx;

        cf = ResolveFmt(&fmt->locale, fmt->number.numberingSystem);
        (void)I18nPluralOperandsFromDecimal(value, &ops);
        (void)I18nPluralSelect(&fmt->locale, RoboI18nPluralCardinal, &ops,
                                      &cat);
        if (cf) {
            idx = (int)cat;
            if (idx < 0 || idx > 5) {
                idx = 5;
            }
            if (cf->unitPattern[idx] >= 0) {
                up = CurrString(cf->unitPattern[idx]);
            } else if (cf->unitPattern[5] >= 0) {
                up = CurrString(cf->unitPattern[5]);
            }
            if (!up) {
                up = "{0} {1}";
            }
        }
        r = ExpandUnitPattern(up, numbuf, numLen, currbuf, currLen, buf, bufCap,
                                &want);
        if (negative && value->kind == RoboI18nDecimalNumber) {
            /* Prepend minus to NAME style when not already signed. */
            char tmp[384];
            size_t tl = 0u;
            const char *minus = fmt->number.minusSign ? fmt->number.minusSign : "-";
            if (r != RoboOk && r != RoboError) {
                return r;
            }
            /* Re-expand into tmp then prefix — only when writing */
            r = ExpandUnitPattern(up, numbuf, numLen, currbuf, currLen, tmp,
                                    sizeof(tmp), &tl);
            if (r != RoboOk) {
                return r;
            }
            {
                size_t mlen = strlen(minus);
                size_t pos = 0u;
                int writing = (buf != NULL && bufCap > 0u);
                Result ar;
                ar = AppendBytes(buf, bufCap, &pos, minus, mlen, writing);
                if (ar != RoboOk && ar != RoboError) {
                    return ar;
                }
                ar = AppendBytes(buf, bufCap, &pos, tmp, tl, writing);
                if (ar != RoboOk && ar != RoboError) {
                    return ar;
                }
                if (writing) {
                    if (pos + 1u > bufCap) {
                        *outLen = pos;
                        return RoboError;
                    }
                    buf[pos] = '\0';
                }
                *outLen = pos;
                return RoboOk;
            }
        }
        *outLen = want;
        return r;
    }

    cf = ResolveFmt(&fmt->locale, fmt->number.numberingSystem);
    if (!cf) {
        /* Minimal fallback: code + space + number */
        size_t pos = 0u;
        int writing = (buf != NULL && bufCap > 0u);
        if (negative) {
            const char *minus = fmt->number.minusSign ? fmt->number.minusSign : "-";
            r = AppendBytes(buf, bufCap, &pos, minus, strlen(minus), writing);
        }
        r = AppendBytes(buf, bufCap, &pos, currbuf, currLen, writing);
        r = AppendBytes(buf, bufCap, &pos, "\xC2\xA0", 2u, writing);
        r = AppendBytes(buf, bufCap, &pos, numbuf, numLen, writing);
        if (writing && pos + 1u <= bufCap) {
            buf[pos] = '\0';
        }
        *outLen = pos;
        return (writing && pos + 1u > bufCap) ? RoboError : RoboOk;
    }

    pattern = CurrString(fmt->accounting ? cf->accounting : cf->standard);
    if (SymbolIsAlphanumeric(currbuf) ||
        fmt->options.style == RoboI18nCurrencyStyleCode) {
        int32_t alt = fmt->accounting ? cf->accountingAlpha : cf->standardAlpha;
        if (alt >= 0) {
            const char *ap = CurrString(alt);
            if (ap && ap[0]) {
                pattern = ap;
            }
        }
    }
    if (!pattern) {
        return RoboError;
    }

    semi = strchr(pattern, ';');
    if (semi) {
        char posbuf[160];
        if (negative) {
            patPos = semi + 1;
            prependMinus = 0;
        } else {
            size_t n = (size_t)(semi - pattern);
            if (n >= sizeof(posbuf)) {
                n = sizeof(posbuf) - 1u;
            }
            memcpy(posbuf, pattern, n);
            posbuf[n] = '\0';
            patPos = posbuf;
            prependMinus = 0;
            return ExpandCurrencyPattern(fmt, patPos, numbuf, numLen, currbuf,
                                           currLen, prependMinus, buf, bufCap,
                                           outLen);
        }
    } else {
        patPos = pattern;
        prependMinus = negative ? 1 : 0;
    }

    return ExpandCurrencyPattern(fmt, patPos, numbuf, numLen, currbuf, currLen,
                                   prependMinus, buf, bufCap, outLen);
}

Result I18nCurrencyFormatI64(const I18nCurrencyFormatter *fmt,
                                            int64_t majorUnits,
                                            char *buf,
                                            size_t bufCap,
                                            size_t *outLen)
{
    I18nDecimal dec;
    Result r = I18nDecimalFromI64(majorUnits, &dec);
    if (r != RoboOk) {
        return r;
    }
    return I18nCurrencyFormat(fmt, &dec, buf, bufCap, outLen);
}

Result I18nCurrencyFormatMinor(const I18nCurrencyFormatter *fmt,
                                              int64_t minorUnits,
                                              char *buf,
                                              size_t bufCap,
                                              size_t *outLen)
{
    I18nDecimal dec;
    char digs[32];
    size_t dn = 0u;
    int negative = 0;
    int64_t v;
    int digits;

    if (!fmt || !fmt->ready || !outLen) {
        return RoboInvalidArgument;
    }
    digits = (int)EffectiveDigits(&fmt->info, fmt->options.usage);
    v = minorUnits;
    if (v < 0) {
        negative = 1;
        if (v == INT64_MIN) {
            return RoboNotSupported;
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
    if (dn > RoboI18nDecimalDigitsMax) {
        return RoboNotSupported;
    }
    memset(&dec, 0, sizeof(dec));
    dec.kind = RoboI18nDecimalNumber;
    dec.negative = (uint8_t)negative;
    dec.scale = (uint8_t)digits;
    dec.digitCount = (uint8_t)dn;
    memcpy(dec.digits, digs, dn);
    return I18nCurrencyFormat(fmt, &dec, buf, bufCap, outLen);
}

Result I18nCurrencyFormatDouble(const I18nCurrencyFormatter *fmt,
                                               double value,
                                               char *buf,
                                               size_t bufCap,
                                               size_t *outLen)
{
    I18nDecimal dec;
    Result r = I18nDecimalFromDouble(value, &dec);
    if (r != RoboOk) {
        return r;
    }
    return I18nCurrencyFormat(fmt, &dec, buf, bufCap, outLen);
}

Result I18nCurrencyFormatToString(
    const I18nCurrencyFormatter *fmt,
    const I18nDecimal *value,
    String *out)
{
    char stack[256];
    size_t len = 0u;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nCurrencyFormat(fmt, value, stack, sizeof(stack), &len);
    if (r == RoboOk) {
        return StringAppend(out, stack, len);
    }
    if (r == RoboError) {
        char big[2048];
        r = I18nCurrencyFormat(fmt, value, big, sizeof(big), &len);
        if (r != RoboOk) {
            return r;
        }
        return StringAppend(out, big, len);
    }
    return r;
}

const char *I18nCurrencyCldrVersionString(void)
{
    return CurrCldrVersionString();
}
