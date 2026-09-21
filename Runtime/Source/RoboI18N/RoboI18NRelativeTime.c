/*
 * RoboI18N relative-time formatter (Goal 4.7).
 */

#include "RoboI18N/RoboI18NRelativeTime.h"

#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboRelData.h"

#include <string.h>

void I18nRelativeOptionsInit(I18nRelativeOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->width = RoboI18nRelWidthLong;
    opts->numeric = RoboI18nRelNumericAuto;
}

Result I18nRelativeTimeInit(I18nRelativeTime *out,
                                           int64_t value,
                                           I18nRelativeUnit unit)
{
    if (!out || (unsigned)unit > (unsigned)RoboI18nRelSecond) {
        return RoboInvalidArgument;
    }
    out->value = value;
    out->unit = unit;
    return RoboOk;
}

Result I18nRelativeFormatterInit(I18nRelativeFormatter *fmt,
                                                const I18nLocale *locale,
                                                const I18nRelativeOptions *opts)
{
    I18nNumberOptions nopts;
    Result r;

    if (!fmt || !locale) {
        return RoboInvalidArgument;
    }
    memset(fmt, 0, sizeof(*fmt));
    fmt->locale = *locale;
    if (opts) {
        fmt->options = *opts;
    } else {
        I18nRelativeOptionsInit(&fmt->options);
    }
    if ((unsigned)fmt->options.width > (unsigned)RoboI18nRelWidthNarrow) {
        return RoboInvalidArgument;
    }

    I18nNumberOptionsInit(&nopts);
    nopts.useGrouping = 0;
    nopts.minIntegerDigits = 1;
    nopts.maxFractionDigits = 0;
    if (fmt->options.numberingSystem[0]) {
        size_t n = strlen(fmt->options.numberingSystem);
        if (n >= sizeof(nopts.numberingSystem)) {
            return RoboInvalidArgument;
        }
        memcpy(nopts.numberingSystem, fmt->options.numberingSystem, n + 1u);
    }
    r = I18nNumberFormatterInit(&fmt->number, &fmt->locale, &nopts);
    if (r != RoboOk) {
        return r;
    }
    fmt->ready = 1;
    return RoboOk;
}

void I18nRelativeFormatterDestroy(I18nRelativeFormatter *fmt)
{
    if (!fmt) {
        return;
    }
    I18nNumberFormatterDestroy(&fmt->number);
    memset(fmt, 0, sizeof(*fmt));
}

static void FormatLocaleTag(const I18nLocale *loc, char *buf, size_t cap)
{
    size_t n = 0;
    size_t ln;
    if (!loc || !buf || cap == 0) {
        return;
    }
    ln = strlen(loc->language);
    if (ln >= cap) {
        ln = cap - 1u;
    }
    memcpy(buf, loc->language, ln);
    n = ln;
    buf[n] = '\0';
    if (loc->script[0] && n + 5u < cap) {
        buf[n++] = '-';
        memcpy(buf + n, loc->script, 4);
        n += 4;
        buf[n] = '\0';
    }
    if (loc->region[0] && n + 1u + strlen(loc->region) < cap) {
        buf[n++] = '-';
        memcpy(buf + n, loc->region, strlen(loc->region));
        n += strlen(loc->region);
        buf[n] = '\0';
    }
}

static const RelUnit *RelResolve(const I18nLocale *loc,
                                          uint8_t unit,
                                          uint8_t width)
{
    I18nLocale cur;
    int guard = 0;
    char tag[64];
    const RelUnit *row;

    if (!loc) {
        return NULL;
    }
    cur = *loc;
    while (guard++ < 16) {
        FormatLocaleTag(&cur, tag, sizeof(tag));
        row = RelUnitFind(tag, unit, width);
        if (row) {
            return row;
        }
        row = RelUnitFind(cur.language, unit, width);
        if (row) {
            return row;
        }
        {
            I18nLocale parent;
            if (I18nLocaleDataParent(&cur, &parent) != RoboOk) {
                break;
            }
            cur = parent;
        }
    }
    return RelUnitFind("en", unit, width);
}

static Result AppendBytes(char *buf,
                                  size_t bufCap,
                                  size_t *pos,
                                  const char *bytes,
                                  size_t len,
                                  int writing)
{
    if (!pos) {
        return RoboInvalidArgument;
    }
    if (writing) {
        if (!buf || bufCap == 0) {
            return RoboInvalidArgument;
        }
        if (*pos + len + 1u > bufCap) {
            return RoboError;
        }
        memcpy(buf + *pos, bytes, len);
    }
    *pos += len;
    return RoboOk;
}

static int64_t I64Abs(int64_t v)
{
    return v < 0 ? -v : v;
}

static const char *PatternForCategory(const int32_t pats[6],
                                        I18nPluralCategory cat)
{
    int32_t idx;
    if ((unsigned)cat > 5u) {
        cat = RoboI18nPluralOther;
    }
    idx = pats[(unsigned)cat];
    if (idx < 0) {
        idx = pats[RoboI18nPluralOther];
    }
    if (idx < 0) {
        return NULL;
    }
    return RelStr(idx);
}

static Result ExpandRelativePattern(const I18nRelativeFormatter *fmt,
                                             const char *pattern,
                                             int64_t absValue,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen)
{
    size_t pos = 0;
    size_t i = 0;
    size_t plen;
    int writing = (buf != NULL && bufCap > 0);
    Result r;

    if (!pattern || !outLen) {
        return RoboInvalidArgument;
    }
    plen = strlen(pattern);
    while (i < plen) {
        if (pattern[i] == '{' && i + 2u < plen && pattern[i + 1u] == '0' &&
            pattern[i + 2u] == '}') {
            char num[48];
            size_t nlen = 0;
            r = I18nNumberFormatI64(&fmt->number, absValue, num, sizeof(num), &nlen);
            if (r != RoboOk) {
                return r;
            }
            r = AppendBytes(buf, bufCap, &pos, num, nlen, writing);
            if (r != RoboOk) {
                *outLen = pos;
                return r;
            }
            i += 3u;
            continue;
        }
        r = AppendBytes(buf, bufCap, &pos, pattern + i, 1u, writing);
        if (r != RoboOk) {
            *outLen = pos;
            return r;
        }
        i++;
    }
    *outLen = pos;
    if (writing) {
        if (pos + 1u > bufCap) {
            return RoboError;
        }
        buf[pos] = '\0';
    }
    return RoboOk;
}

Result I18nRelativeTimeFormat(const I18nRelativeFormatter *fmt,
                                             const I18nRelativeTime *rel,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen)
{
    const RelUnit *row;
    int64_t absV;
    int past;
    const char *pattern = NULL;
    I18nPluralCategory cat = RoboI18nPluralOther;
    Result r;

    if (!fmt || !fmt->ready || !rel || !outLen) {
        return RoboInvalidArgument;
    }
    if ((unsigned)rel->unit > (unsigned)RoboI18nRelSecond) {
        return RoboNotSupported;
    }

    row = RelResolve(&fmt->locale, (uint8_t)rel->unit, (uint8_t)fmt->options.width);
    if (!row) {
        return RoboNotFound;
    }

    absV = I64Abs(rel->value);
    past = rel->value < 0;

    if (fmt->options.numeric == RoboI18nRelNumericAuto && absV <= 2) {
        int32_t idx = -1;
        if (rel->value == -2) {
            idx = row->relativeM2;
        } else if (rel->value == -1) {
            idx = row->relativeM1;
        } else if (rel->value == 0) {
            idx = row->relative0;
        } else if (rel->value == 1) {
            idx = row->relative1;
        } else if (rel->value == 2) {
            idx = row->relative2;
        }
        if (idx >= 0) {
            pattern = RelStr(idx);
            return ExpandRelativePattern(fmt, pattern, absV, buf, bufCap, outLen);
        }
    }

    r = I18nPluralCardinalI64(&fmt->locale, absV, &cat);
    if (r != RoboOk) {
        return r;
    }
    pattern = PatternForCategory(past ? row->past : row->future, cat);
    if (!pattern) {
        return RoboNotFound;
    }
    return ExpandRelativePattern(fmt, pattern, absV, buf, bufCap, outLen);
}

Result I18nRelativeTimeFormatUnit(const I18nRelativeFormatter *fmt,
                                                  int64_t value,
                                                  I18nRelativeUnit unit,
                                                  char *buf,
                                                  size_t bufCap,
                                                  size_t *outLen)
{
    I18nRelativeTime rel;
    Result r = I18nRelativeTimeInit(&rel, value, unit);
    if (r != RoboOk) {
        return r;
    }
    return I18nRelativeTimeFormat(fmt, &rel, buf, bufCap, outLen);
}

Result I18nRelativeTimeFormatToString(
    const I18nRelativeFormatter *fmt,
    const I18nRelativeTime *rel,
    String *out)
{
    char stack[256];
    size_t len = 0;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nRelativeTimeFormat(fmt, rel, stack, sizeof(stack), &len);
    if (r == RoboOk) {
        return StringAppend(out, stack, len);
    }
    return r;
}
