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

const char *I18nPluralCategoryName(I18nPluralCategory cat)
{
    if ((unsigned)cat > 5u) {
        return "";
    }
    return CAT_NAMES[cat];
}

Result I18nPluralCategoryFromName(const char *name,
                                                  I18nPluralCategory *out)
{
    size_t i;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!name || !name[0]) {
        *out = RoboI18nPluralOther;
        return RoboNotFound;
    }
    for (i = 0; i < 6u; i++) {
        if (strcmp(CAT_NAMES[i], name) == 0) {
            *out = (I18nPluralCategory)i;
            return RoboOk;
        }
    }
    *out = RoboI18nPluralOther;
    return RoboNotFound;
}

static void FormatTagLang(const I18nLocale *loc, char *buf, size_t cap)
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

static uint32_t ReadU32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t DigitsMod(const char *digits, size_t len, uint32_t mod)
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

static int DigitsEqU32(const char *digits, size_t len, uint32_t v)
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

static int FracAllZero(const I18nPluralOperands *ops)
{
    size_t i;
    for (i = 0; i < ops->v; i++) {
        if (ops->fDigits[i] != '0') {
            return 0;
        }
    }
    return 1;
}

static void Trim(const I18nPluralOperands *ops, char *out, size_t *outLen)
{
    size_t n = ops->v;
    while (n > 0 && ops->fDigits[n - 1u] == '0') {
        n--;
    }
    if (n == 0) {
        out[0] = '0';
        *outLen = 1;
        return;
    }
    memcpy(out, ops->fDigits, n);
    *outLen = n;
}

/*
 * Obtain an integer operand value for range tests.
 * For n: only defined as integer when fraction is all zeros (then equals i).
 * Returns 0 and sets *ok=0 if operand is not an integer for range matching.
 */
static int OperandInt(const I18nPluralOperands *ops,
                       int operand,
                       uint32_t mod,
                       uint32_t *out,
                       int *ok)
{
    char tbuf[RoboI18nDecimalDigitsMax];
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
            val = DigitsMod(ops->iDigits, ops->iLen, mod);
        } else if (ops->iLen <= 9) {
            size_t i;
            for (i = 0; i < ops->iLen; i++) {
                val = val * 10u + (uint32_t)(ops->iDigits[i] - '0');
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
            val = DigitsMod(ops->fDigits, ops->v, mod);
        } else if (ops->v <= 9) {
            size_t i;
            for (i = 0; i < ops->v; i++) {
                val = val * 10u + (uint32_t)(ops->fDigits[i] - '0');
            }
        } else {
            *ok = 0;
            return 0;
        }
        break;
    case OP_T:
        Trim(ops, tbuf, &tlen);
        if (mod) {
            val = DigitsMod(tbuf, tlen, mod);
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
        if (!FracAllZero(ops)) {
            *ok = 0;
            return 0;
        }
        if (mod) {
            val = DigitsMod(ops->iDigits, ops->iLen, mod);
        } else if (ops->iLen <= 9) {
            size_t i;
            for (i = 0; i < ops->iLen; i++) {
                val = val * 10u + (uint32_t)(ops->iDigits[i] - '0');
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

static int MatchRangesU32(uint32_t val, const uint8_t *p, uint8_t nRanges)
{
    uint8_t i;
    for (i = 0; i < nRanges; i++) {
        uint32_t lo = ReadU32(p + (size_t)i * 8u);
        uint32_t hi = ReadU32(p + (size_t)i * 8u + 4u);
        if (val >= lo && val <= hi) {
            return 1;
        }
    }
    return 0;
}

static int MatchRangesDigits(const char *digits,
                               size_t len,
                               const uint8_t *p,
                               uint8_t nRanges)
{
    uint8_t i;
    for (i = 0; i < nRanges; i++) {
        uint32_t lo = ReadU32(p + (size_t)i * 8u);
        uint32_t hi = ReadU32(p + (size_t)i * 8u + 4u);
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
        } else if (DigitsEqU32(digits, len, lo)) {
            return 1;
        }
    }
    return 0;
}

static int EvalTest(const I18nPluralOperands *ops, const uint8_t **pp)
{
    const uint8_t *p = *pp;
    int operand = p[0];
    int flags = p[1];
    uint32_t mod = ReadU32(p + 2);
    uint8_t nRanges = p[6];
    const uint8_t *ranges = p + 7;
    int negated = (flags & 1) != 0;
    int hasMod = (flags & 2) != 0;
    int matched = 0;
    int ok = 0;
    uint32_t val = 0;

    *pp = ranges + (size_t)nRanges * 8u;

    if (!hasMod) {
        mod = 0;
    }

    /* Exact large-integer equality without mod for n/i */
    if (!hasMod && (operand == OP_N || operand == OP_I) &&
        (operand != OP_N || FracAllZero(ops))) {
        matched = MatchRangesDigits(ops->iDigits, ops->iLen, ranges, nRanges);
        return negated ? !matched : matched;
    }

    if (!OperandInt(ops, operand, hasMod ? mod : 0, &val, &ok) || !ok) {
        /* Non-integer n (or oversized): cannot match integer ranges. */
        matched = 0;
        return negated ? !matched : matched;
    }
    matched = MatchRangesU32(val, ranges, nRanges);
    return negated ? !matched : matched;
}

static int EvalBytecode(const I18nPluralOperands *ops, const uint8_t *code, size_t len)
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
                int r = EvalTest(ops, &q);
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

static const PluralLocaleIndex *LookupKind(I18nPluralKind kind,
                                                     const char *tag)
{
    if (kind == RoboI18nPluralOrdinal) {
        return PluralOrdinalFind(tag);
    }
    return PluralCardinalFind(tag);
}

static void FormatTagLangRegion(const I18nLocale *loc, char *buf, size_t cap)
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

static void FormatTagLangScript(const I18nLocale *loc, char *buf, size_t cap)
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

static const PluralLocaleIndex *ResolveIndex(const I18nLocale *locale,
                                                        I18nPluralKind kind)
{
    I18nLocale cur;
    int guard = 0;
    char tag[RoboI18nTagMax];
    const PluralLocaleIndex *idx = NULL;

    if (!locale || locale->language[0] == '\0') {
        return NULL;
    }
    cur = *locale;
    cur.extensions[0] = '\0';
    cur.privateuse[0] = '\0';
    cur.variantCount = 0;

    while (guard++ < 16) {
        /* Most-specific CLDR keys first (e.g. pt-PT before pt). */
        FormatTagLangRegion(&cur, tag, sizeof(tag));
        if (tag[0]) {
            idx = LookupKind(kind, tag);
            if (idx) {
                return idx;
            }
        }
        FormatTagLangScript(&cur, tag, sizeof(tag));
        if (tag[0]) {
            idx = LookupKind(kind, tag);
            if (idx) {
                return idx;
            }
        }
        FormatTagLang(&cur, tag, sizeof(tag));
        idx = LookupKind(kind, tag);
        if (idx) {
            return idx;
        }
        if (strcmp(cur.language, "root") == 0) {
            break;
        }
        {
            I18nLocale parent;
            if (I18nLocaleDataParent(&cur, &parent) != RoboOk) {
                break;
            }
            cur = parent;
        }
    }
    return NULL;
}

Result I18nPluralOperandsFromDecimal(const I18nDecimal *dec,
                                                     I18nPluralOperands *out)
{
    size_t scale;
    size_t count;
    size_t rawInt;
    size_t i;

    if (!dec || !out) {
        return RoboInvalidArgument;
    }
    if (dec->kind != RoboI18nDecimalNumber) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->negativeInput = dec->negative;
    scale = dec->scale;
    count = dec->digitCount;
    if (count == 0) {
        out->iDigits[0] = '0';
        out->iLen = 1;
        out->v = 0;
        out->w = 0;
        return RoboOk;
    }
    rawInt = (count > scale) ? (count - scale) : 0;
    if (rawInt == 0) {
        out->iDigits[0] = '0';
        out->iLen = 1;
    } else {
        memcpy(out->iDigits, dec->digits, rawInt);
        out->iLen = (uint8_t)rawInt;
        /* strip leading zeros */
        while (out->iLen > 1u && out->iDigits[0] == '0') {
            memmove(out->iDigits, out->iDigits + 1, out->iLen - 1u);
            out->iLen--;
        }
    }
    out->v = (uint8_t)scale;
    if (scale > 0) {
        size_t have = (count > rawInt) ? (count - rawInt) : 0;
        if (have >= scale) {
            memcpy(out->fDigits, dec->digits + rawInt, scale);
        } else {
            if (have > 0) {
                memcpy(out->fDigits, dec->digits + rawInt, have);
            }
            for (i = have; i < scale; i++) {
                out->fDigits[i] = '0';
            }
        }
        /* w = visible fraction without trailing zeros */
        {
            size_t w = scale;
            while (w > 0 && out->fDigits[w - 1u] == '0') {
                w--;
            }
            out->w = (uint8_t)w;
        }
    }
    out->e = 0;
    return RoboOk;
}

Result I18nPluralOperandsFromString(const char *text,
                                                    size_t len,
                                                    I18nPluralOperands *out)
{
    I18nDecimal dec;
    size_t i = 0;
    size_t start;
    size_t dot = (size_t)-1;
    int neg = 0;

    if (!text || !out) {
        return RoboInvalidArgument;
    }
    memset(&dec, 0, sizeof(dec));
    dec.kind = RoboI18nDecimalNumber;
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
                return RoboParseError;
            }
            dot = i;
        } else if (text[i] < '0' || text[i] > '9') {
            break;
        }
    }
    if (i == start || (dot == start && i == start + 1)) {
        return RoboParseError;
    }
    {
        size_t intStart = start;
        size_t intEnd = (dot == (size_t)-1) ? i : dot;
        size_t fracStart = (dot == (size_t)-1) ? i : dot + 1;
        size_t fracEnd = i;
        size_t intLen = intEnd - intStart;
        size_t fracLen = fracEnd - fracStart;
        char digits[RoboI18nDecimalDigitsMax];
        size_t n = 0;
        size_t j;

        if (intLen == 0) {
            digits[n++] = '0';
        } else {
            /* skip leading zeros but keep one */
            j = intStart;
            while (j + 1 < intEnd && text[j] == '0') {
                j++;
            }
            while (j < intEnd) {
                if (n >= RoboI18nDecimalDigitsMax) {
                    return RoboInvalidArgument;
                }
                digits[n++] = text[j++];
            }
        }
        for (j = fracStart; j < fracEnd; j++) {
            if (n >= RoboI18nDecimalDigitsMax) {
                return RoboInvalidArgument;
            }
            digits[n++] = text[j];
        }
        dec.negative = (uint8_t)neg;
        dec.scale = (uint8_t)fracLen;
        dec.digitCount = (uint8_t)n;
        memcpy(dec.digits, digits, n);
    }
    return I18nPluralOperandsFromDecimal(&dec, out);
}

Result I18nPluralSelect(const I18nLocale *locale,
                                     I18nPluralKind kind,
                                     const I18nPluralOperands *ops,
                                     I18nPluralCategory *outCat)
{
    const PluralLocaleIndex *idx;
    const uint8_t *p;
    const uint8_t *end;

    if (!locale || !ops || !outCat) {
        return RoboInvalidArgument;
    }
    idx = ResolveIndex(locale, kind);
    if (!idx) {
        *outCat = RoboI18nPluralOther;
        return RoboOk;
    }
    p = PluralBytecode + idx->offset;
    end = p + idx->length;
    while (p < end) {
        uint8_t cat = *p++;
        uint16_t blen;
        if (cat == 0xFF) {
            break;
        }
        if (p + 2 > end) {
            return RoboError;
        }
        blen = (uint16_t)(p[0] | (p[1] << 8));
        p += 2;
        if (p + blen > end) {
            return RoboError;
        }
        if (EvalBytecode(ops, p, blen)) {
            *outCat = (I18nPluralCategory)cat;
            return RoboOk;
        }
        p += blen;
    }
    *outCat = RoboI18nPluralOther;
    return RoboOk;
}

Result I18nPluralCardinal(const I18nLocale *locale,
                                        const I18nDecimal *number,
                                        I18nPluralCategory *outCat)
{
    I18nPluralOperands ops;
    Result r = I18nPluralOperandsFromDecimal(number, &ops);
    if (r != RoboOk) {
        return r;
    }
    return I18nPluralSelect(locale, RoboI18nPluralCardinal, &ops, outCat);
}

Result I18nPluralOrdinal(const I18nLocale *locale,
                                       const I18nDecimal *number,
                                       I18nPluralCategory *outCat)
{
    I18nPluralOperands ops;
    Result r = I18nPluralOperandsFromDecimal(number, &ops);
    if (r != RoboOk) {
        return r;
    }
    return I18nPluralSelect(locale, RoboI18nPluralOrdinal, &ops, outCat);
}

Result I18nPluralCardinalI64(const I18nLocale *locale,
                                            int64_t value,
                                            I18nPluralCategory *outCat)
{
    I18nDecimal dec;
    Result r = I18nDecimalFromI64(value, &dec);
    if (r != RoboOk) {
        return r;
    }
    return I18nPluralCardinal(locale, &dec, outCat);
}

Result I18nPluralOrdinalI64(const I18nLocale *locale,
                                           int64_t value,
                                           I18nPluralCategory *outCat)
{
    I18nDecimal dec;
    Result r = I18nDecimalFromI64(value, &dec);
    if (r != RoboOk) {
        return r;
    }
    return I18nPluralOrdinal(locale, &dec, outCat);
}

int I18nPluralHasRules(const I18nLocale *locale,
                               I18nPluralKind kind)
{
    if (!locale || locale->language[0] == '\0') {
        return 0;
    }
    return ResolveIndex(locale, kind) != NULL;
}

Result I18nPluralCategories(const I18nLocale *locale,
                                          I18nPluralKind kind,
                                          I18nPluralCategory *out,
                                          size_t cap,
                                          size_t *outCount)
{
    const PluralLocaleIndex *idx;
    const uint8_t *p;
    const uint8_t *end;
    size_t n = 0;
    int sawOther = 0;

    if (!locale || !outCount) {
        return RoboInvalidArgument;
    }
    idx = ResolveIndex(locale, kind);
    if (!idx) {
        if (cap > 0 && out) {
            out[0] = RoboI18nPluralOther;
        }
        *outCount = 1;
        return RoboOk;
    }
    p = PluralBytecode + idx->offset;
    end = p + idx->length;
    while (p < end) {
        uint8_t cat = *p++;
        uint16_t blen;
        if (cat == 0xFF) {
            break;
        }
        if (p + 2 > end) {
            return RoboError;
        }
        blen = (uint16_t)(p[0] | (p[1] << 8));
        p += 2 + blen;
        if (cat == (uint8_t)RoboI18nPluralOther) {
            sawOther = 1;
        }
        if (n < cap && out) {
            out[n] = (I18nPluralCategory)cat;
        }
        n++;
    }
    if (!sawOther) {
        if (n < cap && out) {
            out[n] = RoboI18nPluralOther;
        }
        n++;
    }
    *outCount = n;
    return RoboOk;
}
