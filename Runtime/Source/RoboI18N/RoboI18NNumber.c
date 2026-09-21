/*
 * CLDR-backed decimal number formatting (Goal 4.3).
 */

#include "RoboI18N/RoboI18NNumber.h"

#include "RoboI18N/RoboCldrData.h"
#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NLocaleService.h"

#include <string.h>

static void CopyId(char *dst, size_t cap, const char *src)
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

static void FormatTagSimple(const I18nLocale *loc, char *buf, size_t cap)
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

void I18nNumberOptionsInit(I18nNumberOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->minIntegerDigits = 1;
    opts->minFractionDigits = 0;
    opts->maxFractionDigits = -1;
    opts->useGrouping = 1;
    opts->showPlus = 0;
    opts->rounding = RoboI18nRoundHalfEven;
}

static const CldrNumfmt *LookupNumfmt(const I18nLocale *locale,
                                               const char *nsysId)
{
    I18nLocale cur;
    int guard = 0;
    int32_t nsysKey;

    if (!locale || !nsysId || nsysId[0] == '\0') {
        return NULL;
    }
    nsysKey = CldrFindStr(nsysId);
    if (nsysKey < 0) {
        return NULL;
    }

    cur = *locale;
    cur.extensions[0] = '\0';
    cur.privateuse[0] = '\0';
    cur.variantCount = 0;

    while (guard++ < 16) {
        char tag[RoboI18nTagMax];
        int32_t locKey;
        const CldrNumfmt *row;
        I18nLocale parent;

        if (strcmp(cur.language, "root") == 0) {
            locKey = CldrFindStr("und");
            if (locKey >= 0) {
                row = CldrNumfmtFind(locKey, nsysKey);
                if (row) {
                    return row;
                }
            }
            break;
        }
        FormatTagSimple(&cur, tag, sizeof(tag));
        locKey = CldrFindStr(tag);
        if (locKey >= 0) {
            row = CldrNumfmtFind(locKey, nsysKey);
            if (row) {
                return row;
            }
        }
        if (I18nLocaleDataParent(&cur, &parent) != RoboOk) {
            break;
        }
        cur = parent;
    }
    return NULL;
}

Result I18nNumberFormatterInit(I18nNumberFormatter *fmt,
                                              const I18nLocale *locale,
                                              const I18nNumberOptions *opts)
{
    I18nNumberOptions localOpts;
    char nsys[16];
    const CldrNumfmt *row;
    const CldrNsys *ns;
    int32_t nsysKey;

    if (!fmt || !locale || locale->language[0] == '\0') {
        return RoboInvalidArgument;
    }
    memset(fmt, 0, sizeof(*fmt));
    if (opts) {
        localOpts = *opts;
    } else {
        I18nNumberOptionsInit(&localOpts);
    }
    if (localOpts.minIntegerDigits < 1) {
        localOpts.minIntegerDigits = 1;
    }
    if (localOpts.minIntegerDigits > (int32_t)RoboI18nDecimalDigitsMax) {
        localOpts.minIntegerDigits = (int32_t)RoboI18nDecimalDigitsMax;
    }
    if (localOpts.minFractionDigits < 0) {
        localOpts.minFractionDigits = 0;
    }
    if (localOpts.maxFractionDigits >= 0 &&
        localOpts.minFractionDigits > localOpts.maxFractionDigits) {
        localOpts.minFractionDigits = localOpts.maxFractionDigits;
    }

    fmt->locale = *locale;
    fmt->options = localOpts;

    if (localOpts.numberingSystem[0]) {
        if (I18nLocaleResolveNumbering(locale, localOpts.numberingSystem, nsys,
                                               sizeof(nsys)) != RoboOk) {
            CopyId(nsys, sizeof(nsys), localOpts.numberingSystem);
        }
    } else if (I18nLocaleResolveNumbering(locale, NULL, nsys, sizeof(nsys)) !=
               RoboOk) {
        CopyId(nsys, sizeof(nsys), "latn");
    }
    CopyId(fmt->numberingSystem, sizeof(fmt->numberingSystem), nsys);

    nsysKey = CldrFindStr(nsys);
    if (nsysKey < 0) {
        return RoboNotFound;
    }
    ns = CldrNsysFind(nsysKey);
    if (!ns) {
        return RoboNotFound;
    }
    fmt->digitGlyphs = CldrStr(ns->digits);

    row = LookupNumfmt(locale, nsys);
    if (!row && strcmp(nsys, "latn") != 0) {
        row = LookupNumfmt(locale, "latn");
    }
    if (!row) {
        int32_t und = CldrFindStr("und");
        int32_t latn = CldrFindStr("latn");
        if (und >= 0 && latn >= 0) {
            row = CldrNumfmtFind(und, latn);
        }
    }
    if (!row) {
        return RoboNotFound;
    }

    fmt->decimalSep = CldrStr(row->decimal);
    fmt->groupSep = CldrStr(row->group);
    fmt->plusSign = CldrStr(row->plusSign);
    fmt->minusSign = CldrStr(row->minusSign);
    fmt->nanSymbol = CldrStr(row->nan);
    fmt->infinitySymbol = CldrStr(row->infinity);
    fmt->minGrouping = row->minGrouping;
    fmt->primaryGroup = row->primaryGroup;
    fmt->secondaryGroup = row->secondaryGroup;
    fmt->patternMaxFraction = row->patternMaxFraction;
    fmt->ready = 1;
    return RoboOk;
}

void I18nNumberFormatterDestroy(I18nNumberFormatter *fmt)
{
    if (fmt) {
        memset(fmt, 0, sizeof(*fmt));
    }
}

Result I18nDecimalFromU64(uint64_t value, I18nDecimal *out)
{
    char tmp[24];
    size_t n = 0;
    uint64_t v;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->kind = RoboI18nDecimalNumber;
    if (value == 0) {
        out->digits[0] = '0';
        out->digitCount = 1;
        return RoboOk;
    }
    v = value;
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10ULL));
        v /= 10ULL;
    }
    if (n > RoboI18nDecimalDigitsMax) {
        return RoboInvalidArgument;
    }
    while (n > 0) {
        out->digits[out->digitCount++] = tmp[--n];
    }
    return RoboOk;
}

Result I18nDecimalFromI64(int64_t value, I18nDecimal *out)
{
    uint64_t mag;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    if (value < 0) {
        mag = (uint64_t)(-(value + 1)) + 1ULL;
        r = I18nDecimalFromU64(mag, out);
        if (r != RoboOk) {
            return r;
        }
        out->negative = 1;
        return RoboOk;
    }
    return I18nDecimalFromU64((uint64_t)value, out);
}

static uint64_t DoubleBits(double x)
{
    uint64_t u;
    memcpy(&u, &x, sizeof(u));
    return u;
}

static int DoubleIsNan(uint64_t bits)
{
    return ((bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) &&
           ((bits & 0x000fffffffffffffULL) != 0ULL);
}

static int DoubleIsInf(uint64_t bits)
{
    return ((bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) &&
           ((bits & 0x000fffffffffffffULL) == 0ULL);
}

Result I18nDecimalFromDouble(double value, I18nDecimal *out)
{
    uint64_t bits;
    int neg;
    double ax;
    size_t i;
    size_t n;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    bits = DoubleBits(value);
    neg = (bits >> 63) != 0;

    if (DoubleIsNan(bits)) {
        out->kind = RoboI18nDecimalNan;
        return RoboOk;
    }
    if (DoubleIsInf(bits)) {
        out->kind = neg ? RoboI18nDecimalNegInfinity : RoboI18nDecimalPosInfinity;
        out->negative = (uint8_t)(neg ? 1 : 0);
        return RoboOk;
    }

    {
        uint64_t ab = bits & 0x7fffffffffffffffULL;
        memcpy(&ax, &ab, sizeof(ax));
    }

    if (ax == 0.0) {
        out->kind = RoboI18nDecimalNumber;
        out->negative = (uint8_t)(neg ? 1 : 0);
        out->digits[0] = '0';
        out->digitCount = 1;
        return RoboOk;
    }
    if (ax >= 1.0e18) {
        return RoboNotSupported;
    }

    {
        const int workFrac = 15;
        double scaled = ax;
        uint64_t ip;
        char tmp[24];

        for (i = 0; i < (size_t)workFrac; i++) {
            scaled *= 10.0;
        }
        if (scaled >= 1.0e19) {
            return RoboNotSupported;
        }
        ip = (uint64_t)scaled;
        if ((scaled - (double)ip) >= 0.5) {
            ip += 1ULL;
        }
        n = 0;
        if (ip == 0) {
            out->digits[0] = '0';
            out->digitCount = 1;
            out->scale = 0;
        } else {
            uint64_t v = ip;
            while (v > 0) {
                tmp[n++] = (char)('0' + (v % 10ULL));
                v /= 10ULL;
            }
            while (n > 0) {
                if (out->digitCount >= RoboI18nDecimalDigitsMax) {
                    return RoboInvalidArgument;
                }
                out->digits[out->digitCount++] = tmp[--n];
            }
            out->scale = (uint8_t)workFrac;
            while (out->digitCount > (size_t)out->scale + 1u && out->digits[0] == '0') {
                memmove(out->digits, out->digits + 1, out->digitCount - 1u);
                out->digitCount--;
            }
        }
        out->kind = RoboI18nDecimalNumber;
        out->negative = (uint8_t)(neg ? 1 : 0);
        return RoboOk;
    }
}

static int DigitGlyphAt(const char *glyphs, int digit, const char **out, size_t *outLen)
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
        *outLen = 1;
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
            *outLen = clen;
            return 1;
        }
        i += clen;
        start = i;
        idx++;
    }
    return 0;
}

static int ClampMaxFrac(const I18nNumberFormatter *fmt)
{
    if (fmt->options.maxFractionDigits < 0) {
        return (int)fmt->patternMaxFraction;
    }
    return (int)fmt->options.maxFractionDigits;
}

static int ShouldRoundUp(int rounding,
                           int negative,
                           int digit,
                           int stickyNonzero,
                           int leastKeptOdd)
{
    switch ((I18nRounding)rounding) {
    case RoboI18nRoundDown:
        return 0;
    case RoboI18nRoundUp:
        return digit > 0 || stickyNonzero;
    case RoboI18nRoundCeiling:
        return !negative && (digit > 0 || stickyNonzero);
    case RoboI18nRoundFloor:
        return negative && (digit > 0 || stickyNonzero);
    case RoboI18nRoundHalfDown:
        if (digit > 5) {
            return 1;
        }
        if (digit < 5) {
            return 0;
        }
        return stickyNonzero;
    case RoboI18nRoundHalfUp:
        return digit >= 5;
    case RoboI18nRoundHalfEven:
    default:
        if (digit > 5) {
            return 1;
        }
        if (digit < 5) {
            return 0;
        }
        if (stickyNonzero) {
            return 1;
        }
        return leastKeptOdd;
    }
}

static Result RoundDecimal(const I18nDecimal *in,
                                   int maxFrac,
                                   int rounding,
                                   I18nDecimal *out)
{
    char digits[RoboI18nDecimalDigitsMax + 4];
    size_t count;
    size_t intLen;
    size_t i;
    int firstDiscard;
    int sticky;
    int leastOdd;
    size_t keep;

    if (!in || !out) {
        return RoboInvalidArgument;
    }
    *out = *in;
    if (in->kind != RoboI18nDecimalNumber) {
        return RoboOk;
    }
    if (maxFrac < 0) {
        maxFrac = 0;
    }
    if (maxFrac > (int)RoboI18nDecimalDigitsMax - 1) {
        maxFrac = (int)RoboI18nDecimalDigitsMax - 1;
    }

    count = in->digitCount;
    if (count == 0) {
        out->digits[0] = '0';
        out->digitCount = 1;
        out->scale = 0;
        return RoboOk;
    }
    memcpy(digits, in->digits, count);
    intLen = (count > in->scale) ? (count - in->scale) : 0;

    if ((int)in->scale <= maxFrac) {
        out->scale = in->scale;
        return RoboOk;
    }

    keep = intLen + (size_t)maxFrac;
    if (keep == 0) {
        firstDiscard = digits[0] - '0';
        sticky = 0;
        for (i = 1; i < count; i++) {
            if (digits[i] != '0') {
                sticky = 1;
                break;
            }
        }
        if (ShouldRoundUp(rounding, in->negative, firstDiscard, sticky, 0)) {
            out->digits[0] = '1';
            out->digitCount = 1;
            out->scale = 0;
        } else {
            out->digits[0] = '0';
            out->digitCount = 1;
            out->scale = 0;
            out->negative = 0;
        }
        return RoboOk;
    }

    firstDiscard = (keep < count) ? (digits[keep] - '0') : 0;
    sticky = 0;
    for (i = keep + 1u; i < count; i++) {
        if (digits[i] != '0') {
            sticky = 1;
            break;
        }
    }
    leastOdd = (digits[keep - 1u] - '0') & 1;

    if (ShouldRoundUp(rounding, in->negative, firstDiscard, sticky, leastOdd)) {
        i = keep;
        while (i > 0) {
            i--;
            if (digits[i] < '9') {
                digits[i]++;
                break;
            }
            digits[i] = '0';
            if (i == 0) {
                if (keep + 1u > RoboI18nDecimalDigitsMax) {
                    return RoboInvalidArgument;
                }
                memmove(digits + 1, digits, keep);
                digits[0] = '1';
                keep++;
                break;
            }
        }
    }

    out->digitCount = (uint8_t)keep;
    memcpy(out->digits, digits, keep);
    out->scale = (uint8_t)maxFrac;
    while (out->digitCount > out->scale + 1u && out->digits[0] == '0') {
        memmove(out->digits, out->digits + 1, out->digitCount - 1u);
        out->digitCount--;
    }
    return RoboOk;
}

static Result AppendBytes(char *buf,
                                  size_t bufCap,
                                  size_t *pos,
                                  const char *bytes,
                                  size_t len,
                                  int writing)
{
    if (!bytes && len) {
        return RoboInvalidArgument;
    }
    if (writing) {
        if (*pos + len + 1u > bufCap) {
            return RoboError;
        }
        memcpy(buf + *pos, bytes, len);
    }
    *pos += len;
    return RoboOk;
}

static int SepBeforeIndex(size_t index,
                            size_t intLen,
                            uint8_t primary,
                            uint8_t secondary)
{
    size_t pos;
    uint8_t step;
    if (index == 0 || primary == 0) {
        return 0;
    }
    pos = intLen - (size_t)primary;
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

static Result EmitIntegerGrouped(const I18nNumberFormatter *fmt,
                                          const char *asciiInt,
                                          size_t intLen,
                                          char *buf,
                                          size_t bufCap,
                                          size_t *pos,
                                          int writing)
{
    size_t i;
    int useGroup;
    Result r;

    useGroup = fmt->options.useGrouping && fmt->primaryGroup > 0 &&
                intLen >= (size_t)fmt->primaryGroup + (size_t)fmt->minGrouping;

    for (i = 0; i < intLen; i++) {
        const char *g;
        size_t glen;
        if (useGroup &&
            SepBeforeIndex(i, intLen, fmt->primaryGroup, fmt->secondaryGroup)) {
            r = AppendBytes(buf, bufCap, pos, fmt->groupSep, strlen(fmt->groupSep),
                             writing);
            if (r != RoboOk) {
                return r;
            }
        }
        if (!DigitGlyphAt(fmt->digitGlyphs, asciiInt[i] - '0', &g, &glen)) {
            return RoboError;
        }
        r = AppendBytes(buf, bufCap, pos, g, glen, writing);
        if (r != RoboOk) {
            return r;
        }
    }
    return RoboOk;
}

static Result FormatInner(const I18nNumberFormatter *fmt,
                                  const I18nDecimal *value,
                                  char *buf,
                                  size_t bufCap,
                                  size_t *outLen,
                                  int writing)
{
    I18nDecimal rounded;
    int maxFrac;
    int minFrac;
    int minInt;
    char intDigits[RoboI18nDecimalDigitsMax + 4];
    char fracDigits[RoboI18nDecimalDigitsMax + 4];
    size_t intLen = 0;
    size_t fracLen = 0;
    size_t pos = 0;
    Result r;

    if (value->kind == RoboI18nDecimalNan) {
        size_t need = strlen(fmt->nanSymbol);
        if (outLen) {
            *outLen = need;
        }
        if (!writing) {
            return RoboOk;
        }
        if (need + 1u > bufCap) {
            return RoboError;
        }
        memcpy(buf, fmt->nanSymbol, need);
        buf[need] = '\0';
        return RoboOk;
    }
    if (value->kind == RoboI18nDecimalPosInfinity ||
        value->kind == RoboI18nDecimalNegInfinity) {
        const char *sign = "";
        size_t sl = 0;
        size_t il = strlen(fmt->infinitySymbol);
        size_t need;
        if (value->kind == RoboI18nDecimalNegInfinity) {
            sign = fmt->minusSign;
            sl = strlen(sign);
        } else if (fmt->options.showPlus) {
            sign = fmt->plusSign;
            sl = strlen(sign);
        }
        need = sl + il;
        if (outLen) {
            *outLen = need;
        }
        if (!writing) {
            return RoboOk;
        }
        if (need + 1u > bufCap) {
            return RoboError;
        }
        memcpy(buf, sign, sl);
        memcpy(buf + sl, fmt->infinitySymbol, il);
        buf[need] = '\0';
        return RoboOk;
    }

    maxFrac = ClampMaxFrac(fmt);
    minFrac = fmt->options.minFractionDigits;
    if (minFrac > maxFrac) {
        minFrac = maxFrac;
    }
    minInt = fmt->options.minIntegerDigits;

    r = RoundDecimal(value, maxFrac, (int)fmt->options.rounding, &rounded);
    if (r != RoboOk) {
        return r;
    }

    {
        size_t scale = rounded.scale;
        size_t count = rounded.digitCount;
        size_t rawInt = (count > scale) ? (count - scale) : 0;
        size_t padInt = (rawInt < (size_t)minInt) ? ((size_t)minInt - rawInt) : 0;
        size_t i;

        for (i = 0; i < padInt; i++) {
            intDigits[intLen++] = '0';
        }
        if (rawInt == 0) {
            if (padInt == 0) {
                intDigits[intLen++] = '0';
            }
        } else {
            memcpy(intDigits + intLen, rounded.digits, rawInt);
            intLen += rawInt;
        }

        if (scale > 0 && count > rawInt) {
            size_t fl = count - rawInt;
            memcpy(fracDigits, rounded.digits + rawInt, fl);
            fracLen = fl;
        }
        while ((int)fracLen < minFrac) {
            fracDigits[fracLen++] = '0';
        }
        if (maxFrac == 0) {
            fracLen = 0;
        }
    }

    if (rounded.negative) {
        r = AppendBytes(buf, bufCap, &pos, fmt->minusSign, strlen(fmt->minusSign), writing);
    } else if (fmt->options.showPlus) {
        r = AppendBytes(buf, bufCap, &pos, fmt->plusSign, strlen(fmt->plusSign), writing);
    } else {
        r = RoboOk;
    }
    if (r != RoboOk) {
        return r;
    }

    r = EmitIntegerGrouped(fmt, intDigits, intLen, buf, bufCap, &pos, writing);
    if (r != RoboOk) {
        return r;
    }

    if (fracLen > 0) {
        size_t i;
        r = AppendBytes(buf, bufCap, &pos, fmt->decimalSep, strlen(fmt->decimalSep), writing);
        if (r != RoboOk) {
            return r;
        }
        for (i = 0; i < fracLen; i++) {
            const char *g;
            size_t glen;
            if (!DigitGlyphAt(fmt->digitGlyphs, fracDigits[i] - '0', &g, &glen)) {
                return RoboError;
            }
            r = AppendBytes(buf, bufCap, &pos, g, glen, writing);
            if (r != RoboOk) {
                return r;
            }
        }
    }

    if (outLen) {
        *outLen = pos;
    }
    if (writing) {
        if (pos + 1u > bufCap) {
            return RoboError;
        }
        buf[pos] = '\0';
    }
    return RoboOk;
}

Result I18nNumberFormat(const I18nNumberFormatter *fmt,
                                      const I18nDecimal *value,
                                      char *buf,
                                      size_t bufCap,
                                      size_t *outLen)
{
    Result r;
    size_t needed = 0;

    if (!fmt || !fmt->ready || !value) {
        return RoboInvalidArgument;
    }
    if (buf == NULL || bufCap == 0) {
        return FormatInner(fmt, value, NULL, 0, outLen, 0);
    }
    r = FormatInner(fmt, value, buf, bufCap, outLen, 1);
    if (r == RoboError) {
        (void)FormatInner(fmt, value, NULL, 0, &needed, 0);
        if (outLen) {
            *outLen = needed;
        }
    }
    return r;
}

Result I18nNumberFormatI64(const I18nNumberFormatter *fmt,
                                          int64_t value,
                                          char *buf,
                                          size_t bufCap,
                                          size_t *outLen)
{
    I18nDecimal dec;
    Result r = I18nDecimalFromI64(value, &dec);
    if (r != RoboOk) {
        return r;
    }
    return I18nNumberFormat(fmt, &dec, buf, bufCap, outLen);
}

Result I18nNumberFormatU64(const I18nNumberFormatter *fmt,
                                          uint64_t value,
                                          char *buf,
                                          size_t bufCap,
                                          size_t *outLen)
{
    I18nDecimal dec;
    Result r = I18nDecimalFromU64(value, &dec);
    if (r != RoboOk) {
        return r;
    }
    return I18nNumberFormat(fmt, &dec, buf, bufCap, outLen);
}

Result I18nNumberFormatDouble(const I18nNumberFormatter *fmt,
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
    return I18nNumberFormat(fmt, &dec, buf, bufCap, outLen);
}

Result I18nNumberFormatToString(const I18nNumberFormatter *fmt,
                                                const I18nDecimal *value,
                                                String *out)
{
    char stack[256];
    size_t len = 0;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nNumberFormat(fmt, value, stack, sizeof(stack), &len);
    if (r == RoboOk) {
        return StringAppend(out, stack, len);
    }
    if (r == RoboError) {
        char big[2048];
        r = I18nNumberFormat(fmt, value, big, sizeof(big), &len);
        if (r != RoboOk) {
            return r;
        }
        return StringAppend(out, big, len);
    }
    return r;
}
