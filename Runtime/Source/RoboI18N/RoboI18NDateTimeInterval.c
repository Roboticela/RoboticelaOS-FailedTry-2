/*
 * RoboI18N date/time interval formatter (Goal 4.7).
 *
 * Pattern selection: skeleton + greatest differing field letter → CLDR
 * intervalFormats row. Split pattern at the first repeated field letter
 * (ICU-style) so repeated fields collapse when CLDR encodes that.
 */

#include "RoboI18N/RoboI18NDateTimeInterval.h"

#include "RoboI18N/RoboCalData.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboI18NLocaleData.h"

#include <string.h>

void I18nIntervalOptionsInit(I18nIntervalOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->normalizeReversed = 0;
}

static int DatetimeCmp(const I18nDatetime *a, const I18nDatetime *b)
{
    int c = I18nDateCompare(&a->date, &b->date);
    int64_t na, nb;
    if (c != 0) {
        return c;
    }
    if (I18nTimeToNanosOfDay(&a->time, &na) != RoboOk ||
        I18nTimeToNanosOfDay(&b->time, &nb) != RoboOk) {
        return 0;
    }
    if (na < nb) {
        return -1;
    }
    if (na > nb) {
        return 1;
    }
    return 0;
}

Result I18nDatetimeIntervalInit(I18nDatetimeInterval *out,
                                               const I18nDatetime *start,
                                               const I18nDatetime *end,
                                               int normalizeReversed)
{
    if (!out || !start || !end) {
        return RoboInvalidArgument;
    }
    if (!I18nDateIsValid(&start->date) || !I18nDateIsValid(&end->date) ||
        !I18nTimeIsValid(&start->time) || !I18nTimeIsValid(&end->time)) {
        return RoboInvalidArgument;
    }
    out->start = *start;
    out->end = *end;
    if (DatetimeCmp(&out->start, &out->end) > 0) {
        if (!normalizeReversed) {
            return RoboInvalidArgument;
        }
        {
            I18nDatetime tmp = out->start;
            out->start = out->end;
            out->end = tmp;
        }
    }
    return RoboOk;
}

Result I18nInstantIntervalInit(I18nInstantInterval *out,
                                              const I18nInstant *start,
                                              const I18nInstant *end,
                                              int normalizeReversed)
{
    if (!out || !start || !end) {
        return RoboInvalidArgument;
    }
    out->start = *start;
    out->end = *end;
    if (I18nInstantCompare(&out->start, &out->end) > 0) {
        if (!normalizeReversed) {
            return RoboInvalidArgument;
        }
        {
            I18nInstant tmp = out->start;
            out->start = out->end;
            out->end = tmp;
        }
    }
    return RoboOk;
}

Result I18nIntervalFormatterInit(I18nIntervalFormatter *fmt,
                                                const I18nLocale *locale,
                                                const I18nCalendar *calendar,
                                                const I18nTimezone *tz,
                                                const I18nIntervalOptions *opts)
{
    Result r;
    if (!fmt || !locale) {
        return RoboInvalidArgument;
    }
    memset(fmt, 0, sizeof(*fmt));
    fmt->locale = *locale;
    if (calendar) {
        fmt->calendar = *calendar;
    } else {
        r = I18nCalendarFromKind(RoboI18nCalGregorian, &fmt->calendar);
        if (r != RoboOk) {
            return r;
        }
    }
    if (!I18nCalendarHasConversion(&fmt->calendar)) {
        return RoboNotSupported;
    }
    if (tz) {
        fmt->timezone = *tz;
    } else {
        r = I18nTimezoneUtc(&fmt->timezone);
        if (r != RoboOk) {
            return r;
        }
    }
    if (opts) {
        fmt->options = *opts;
    } else {
        I18nIntervalOptionsInit(&fmt->options);
    }
    fmt->ready = 1;
    return RoboOk;
}

void I18nIntervalFormatterDestroy(I18nIntervalFormatter *fmt)
{
    if (!fmt) {
        return;
    }
    memset(fmt, 0, sizeof(*fmt));
}

Result I18nIntervalGreatestDifference(const I18nDatetime *start,
                                                     const I18nDatetime *end,
                                                     char *outLetter,
                                                     size_t letterCap)
{
    if (!start || !end || !outLetter || letterCap < 2u) {
        return RoboInvalidArgument;
    }
    outLetter[0] = '\0';
    if (start->date.year != end->date.year) {
        outLetter[0] = 'y';
        outLetter[1] = '\0';
        return RoboOk;
    }
    if (start->date.month != end->date.month) {
        outLetter[0] = 'M';
        outLetter[1] = '\0';
        return RoboOk;
    }
    if (start->date.day != end->date.day) {
        outLetter[0] = 'd';
        outLetter[1] = '\0';
        return RoboOk;
    }
    /* Same calendar day — time fields. Prefer 'a' when am/pm differs for 12h. */
    if ((start->time.hour < 12) != (end->time.hour < 12)) {
        outLetter[0] = 'a';
        outLetter[1] = '\0';
        return RoboOk;
    }
    if (start->time.hour != end->time.hour) {
        outLetter[0] = 'H';
        outLetter[1] = '\0';
        return RoboOk;
    }
    if (start->time.minute != end->time.minute) {
        outLetter[0] = 'm';
        outLetter[1] = '\0';
        return RoboOk;
    }
    if (start->time.second != end->time.second || start->time.nano != end->time.nano) {
        outLetter[0] = 's';
        outLetter[1] = '\0';
        return RoboOk;
    }
    return RoboOk;
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

static const CalIntervalFormat *IntervalFindInherited(
    const I18nLocale *loc,
    const char *calendar,
    const char *skeleton,
    const char *diff)
{
    I18nLocale cur;
    int guard = 0;
    char tag[64];
    const CalIntervalFormat *row;

    cur = *loc;
    while (guard++ < 16) {
        FormatLocaleTag(&cur, tag, sizeof(tag));
        row = CalIntervalFormatFind(tag, calendar, skeleton, diff);
        if (row) {
            return row;
        }
        row = CalIntervalFormatFind(cur.language, calendar, skeleton, diff);
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
    return CalIntervalFormatFind("en", calendar, skeleton, diff);
}

static const CalIntervalFallback *FallbackFindInherited(
    const I18nLocale *loc,
    const char *calendar)
{
    I18nLocale cur;
    int guard = 0;
    char tag[64];
    const CalIntervalFallback *row;

    cur = *loc;
    while (guard++ < 16) {
        FormatLocaleTag(&cur, tag, sizeof(tag));
        row = CalIntervalFallbackFind(tag, calendar);
        if (row) {
            return row;
        }
        row = CalIntervalFallbackFind(cur.language, calendar);
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
    return CalIntervalFallbackFind("en", calendar);
}

static int IsPatternLetter(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/**
 * Split interval pattern at the first repeated pattern letter (second run).
 * Returns 1 on split, 0 if no repeat (caller should use fallback).
 */
static int SplitIntervalPattern(const char *pattern,
                                  char *left,
                                  size_t leftCap,
                                  char *right,
                                  size_t rightCap)
{
    size_t i = 0;
    size_t plen;
    uint8_t seen[128];
    size_t split = (size_t)-1;

    if (!pattern || !left || !right || leftCap == 0 || rightCap == 0) {
        return 0;
    }
    memset(seen, 0, sizeof(seen));
    plen = strlen(pattern);
    while (i < plen) {
        unsigned char c = (unsigned char)pattern[i];
        if (c == '\'') {
            i++;
            while (i < plen) {
                if (pattern[i] == '\'') {
                    if (i + 1u < plen && pattern[i + 1u] == '\'') {
                        i += 2u;
                        continue;
                    }
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        if (IsPatternLetter(c)) {
            size_t j = i + 1u;
            while (j < plen && (unsigned char)pattern[j] == c) {
                j++;
            }
            if (seen[c]) {
                split = i;
                break;
            }
            seen[c] = 1;
            i = j;
            continue;
        }
        i++;
    }
    if (split == (size_t)-1 || split >= plen || split >= leftCap) {
        return 0;
    }
    if (plen - split >= rightCap) {
        return 0;
    }
    memcpy(left, pattern, split);
    left[split] = '\0';
    memcpy(right, pattern + split, plen - split + 1u);
    return 1;
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

static Result FormatOnePattern(const I18nIntervalFormatter *fmt,
                                        const char *pattern,
                                        const I18nDatetime *dt,
                                        char *out,
                                        size_t outCap,
                                        size_t *outLen)
{
    I18nDatetimeFormatter dfmt;
    I18nDtfmtOptions dopts;
    Result r;

    I18nDtfmtOptionsInit(&dopts);
    if (fmt->options.numberingSystem[0]) {
        size_t n = strlen(fmt->options.numberingSystem);
        if (n < sizeof(dopts.numberingSystem)) {
            memcpy(dopts.numberingSystem, fmt->options.numberingSystem, n + 1u);
        }
    }
    r = I18nDatetimeFormatterInitPattern(&dfmt, &fmt->locale, &fmt->calendar,
                                                  &fmt->timezone, pattern, &dopts);
    if (r != RoboOk) {
        return r;
    }
    r = I18nDatetimeFormatDatetime(&dfmt, dt, out, outCap, outLen);
    I18nDatetimeFormatterDestroy(&dfmt);
    return r;
}

static const char *DefaultSkeleton(const I18nDatetimeInterval *iv, char diff)
{
    (void)iv;
    if (diff == 'H' || diff == 'h' || diff == 'm' || diff == 's' || diff == 'a') {
        return "Hm";
    }
    if (diff == 'd' || diff == 'M' || diff == 'y' || diff == '\0') {
        return "yMMMd";
    }
    return "yMMMd";
}

static Result FormatFallback(const I18nIntervalFormatter *fmt,
                                     const I18nDatetimeInterval *iv,
                                     const char *fallbackPat,
                                     const char *singleSkel,
                                     char *buf,
                                     size_t bufCap,
                                     size_t *outLen)
{
    char left[192];
    char right[192];
    size_t llen = 0;
    size_t rlen = 0;
    size_t pos = 0;
    size_t i;
    size_t fplen;
    int writing = (buf != NULL && bufCap > 0);
    Result r;
    const char *skel = singleSkel && singleSkel[0] ? singleSkel : "yMMMd";
    const char *avail = NULL;

    /* Resolve availableFormat for a full single-side format. */
    {
        I18nLocale cur = fmt->locale;
        int guard = 0;
        char tag[64];
        const CalAvailableFormat *row = NULL;
        while (guard++ < 16) {
            FormatLocaleTag(&cur, tag, sizeof(tag));
            row = CalAvailableFormatFind(tag, fmt->calendar.id, skel);
            if (!row) {
                row = CalAvailableFormatFind(cur.language, fmt->calendar.id, skel);
            }
            if (row) {
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
        if (!row) {
            row = CalAvailableFormatFind("en", fmt->calendar.id, skel);
        }
        if (row) {
            avail = CalStr(row->pattern);
        } else {
            avail = "y-MM-dd HH:mm";
        }
    }

    r = FormatOnePattern(fmt, avail, &iv->start, left, sizeof(left), &llen);
    if (r != RoboOk) {
        return r;
    }
    r = FormatOnePattern(fmt, avail, &iv->end, right, sizeof(right), &rlen);
    if (r != RoboOk) {
        return r;
    }

    fplen = strlen(fallbackPat);
    for (i = 0; i < fplen;) {
        if (fallbackPat[i] == '{' && i + 2u < fplen && fallbackPat[i + 2u] == '}') {
            if (fallbackPat[i + 1u] == '0') {
                r = AppendBytes(buf, bufCap, &pos, left, llen, writing);
                if (r != RoboOk) {
                    *outLen = pos;
                    return r;
                }
                i += 3u;
                continue;
            }
            if (fallbackPat[i + 1u] == '1') {
                r = AppendBytes(buf, bufCap, &pos, right, rlen, writing);
                if (r != RoboOk) {
                    *outLen = pos;
                    return r;
                }
                i += 3u;
                continue;
            }
        }
        r = AppendBytes(buf, bufCap, &pos, fallbackPat + i, 1u, writing);
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

Result I18nDatetimeIntervalFormat(const I18nIntervalFormatter *fmt,
                                                 const I18nDatetimeInterval *interval,
                                                 char *buf,
                                                 size_t bufCap,
                                                 size_t *outLen)
{
    I18nDatetimeInterval iv;
    char diff[2];
    const char *skel;
    const CalIntervalFormat *row;
    const char *ipat;
    char left[160];
    char right[160];
    char leftOut[192];
    char rightOut[192];
    size_t llen = 0;
    size_t rlen = 0;
    size_t pos = 0;
    int writing;
    Result r;
    int swap = 0;

    if (!fmt || !fmt->ready || !interval || !outLen) {
        return RoboInvalidArgument;
    }
    writing = (buf != NULL && bufCap > 0);

    r = I18nDatetimeIntervalInit(&iv, &interval->start, &interval->end,
                                         fmt->options.normalizeReversed);
    if (r != RoboOk) {
        return r;
    }

    r = I18nIntervalGreatestDifference(&iv.start, &iv.end, diff, sizeof(diff));
    if (r != RoboOk) {
        return r;
    }

    /* Identical endpoints → single datetime format. */
    if (diff[0] == '\0') {
        I18nDatetimeFormatter dfmt;
        I18nDtfmtOptions dopts;
        const char *useSkel =
            fmt->options.skeleton[0] ? fmt->options.skeleton : DefaultSkeleton(&iv, '\0');
        I18nDtfmtOptionsInit(&dopts);
        if (fmt->options.numberingSystem[0]) {
            size_t n = strlen(fmt->options.numberingSystem);
            if (n < sizeof(dopts.numberingSystem)) {
                memcpy(dopts.numberingSystem, fmt->options.numberingSystem, n + 1u);
            }
        }
        r = I18nDatetimeFormatterInitSkeleton(&dfmt, &fmt->locale, &fmt->calendar,
                                                       &fmt->timezone, useSkel, &dopts);
        if (r != RoboOk) {
            return r;
        }
        r = I18nDatetimeFormatDatetime(&dfmt, &iv.start, buf, bufCap, outLen);
        I18nDatetimeFormatterDestroy(&dfmt);
        return r;
    }

    skel = fmt->options.skeleton[0] ? fmt->options.skeleton : DefaultSkeleton(&iv, diff[0]);
    /* Adjust letter for 12h skeletons preferring h/a. */
    if ((diff[0] == 'H' || diff[0] == 'h') && strchr(skel, 'h') && !strchr(skel, 'H')) {
        diff[0] = 'h';
    }

    row = IntervalFindInherited(&fmt->locale, fmt->calendar.id, skel, diff);
    if (!row) {
        /* Try alternate hour letter. */
        if (diff[0] == 'H') {
            diff[0] = 'h';
            row = IntervalFindInherited(&fmt->locale, fmt->calendar.id, skel, diff);
        } else if (diff[0] == 'h') {
            diff[0] = 'H';
            row = IntervalFindInherited(&fmt->locale, fmt->calendar.id, skel, diff);
        }
    }

    if (!row) {
        const CalIntervalFallback *fb =
            FallbackFindInherited(&fmt->locale, fmt->calendar.id);
        const char *fp = fb ? CalStr(fb->pattern) : "{0} – {1}";
        return FormatFallback(fmt, &iv, fp, skel, buf, bufCap, outLen);
    }

    ipat = CalStr(row->pattern);
    /* Strip earliestFirst:/latestFirst: prefixes. */
    if (strncmp(ipat, "earliestFirst:", 14) == 0) {
        ipat += 14;
    } else if (strncmp(ipat, "latestFirst:", 12) == 0) {
        ipat += 12;
        swap = 1;
    }

    if (!SplitIntervalPattern(ipat, left, sizeof(left), right, sizeof(right))) {
        const CalIntervalFallback *fb =
            FallbackFindInherited(&fmt->locale, fmt->calendar.id);
        const char *fp = fb ? CalStr(fb->pattern) : "{0} – {1}";
        return FormatFallback(fmt, &iv, fp, skel, buf, bufCap, outLen);
    }

    {
        const I18nDatetime *a = swap ? &iv.end : &iv.start;
        const I18nDatetime *b = swap ? &iv.start : &iv.end;
        r = FormatOnePattern(fmt, left, a, leftOut, sizeof(leftOut), &llen);
        if (r != RoboOk) {
            return r;
        }
        r = FormatOnePattern(fmt, right, b, rightOut, sizeof(rightOut), &rlen);
        if (r != RoboOk) {
            return r;
        }
    }

    r = AppendBytes(buf, bufCap, &pos, leftOut, llen, writing);
    if (r != RoboOk) {
        *outLen = pos;
        return r;
    }
    r = AppendBytes(buf, bufCap, &pos, rightOut, rlen, writing);
    if (r != RoboOk) {
        *outLen = pos;
        return r;
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

Result I18nDateIntervalFormat(const I18nIntervalFormatter *fmt,
                                             const I18nDate *start,
                                             const I18nDate *end,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen)
{
    I18nDatetimeInterval iv;
    memset(&iv, 0, sizeof(iv));
    if (!start || !end) {
        return RoboInvalidArgument;
    }
    iv.start.date = *start;
    iv.end.date = *end;
    return I18nDatetimeIntervalFormat(fmt, &iv, buf, bufCap, outLen);
}

Result I18nTimeIntervalFormat(const I18nIntervalFormatter *fmt,
                                             const I18nTime *start,
                                             const I18nTime *end,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen)
{
    I18nDatetimeInterval iv;
    I18nIntervalFormatter local;
    I18nIntervalOptions opts;
    Result r;

    if (!fmt || !start || !end) {
        return RoboInvalidArgument;
    }
    memset(&iv, 0, sizeof(iv));
    /* Anchor on a fixed date so only time fields differ. */
    (void)I18nDateInit(&iv.start.date, 2000, 1, 1);
    (void)I18nDateInit(&iv.end.date, 2000, 1, 1);
    iv.start.time = *start;
    iv.end.time = *end;

    opts = fmt->options;
    if (!opts.skeleton[0]) {
        memcpy(opts.skeleton, "Hm", 3);
    }
    local = *fmt;
    local.options = opts;
    r = I18nDatetimeIntervalFormat(&local, &iv, buf, bufCap, outLen);
    return r;
}

Result I18nInstantIntervalFormat(const I18nIntervalFormatter *fmt,
                                                const I18nInstantInterval *interval,
                                                char *buf,
                                                size_t bufCap,
                                                size_t *outLen)
{
    I18nDatetimeInterval iv;
    I18nTzResolve rs = RoboI18nTzUnique;
    I18nTzResolve re = RoboI18nTzUnique;
    I18nInstantInterval ni;
    Result r;

    if (!fmt || !interval) {
        return RoboInvalidArgument;
    }
    r = I18nInstantIntervalInit(&ni, &interval->start, &interval->end,
                                        fmt->options.normalizeReversed);
    if (r != RoboOk) {
        return r;
    }
    r = I18nTimezoneToLocal(&fmt->timezone, &ni.start, &iv.start, &rs);
    if (r != RoboOk) {
        return r;
    }
    r = I18nTimezoneToLocal(&fmt->timezone, &ni.end, &iv.end, &re);
    if (r != RoboOk) {
        return r;
    }
    if (rs != RoboI18nTzUnique || re != RoboI18nTzUnique) {
        return RoboNotSupported;
    }
    return I18nDatetimeIntervalFormat(fmt, &iv, buf, bufCap, outLen);
}

Result I18nDatetimeIntervalFormatToString(
    const I18nIntervalFormatter *fmt,
    const I18nDatetimeInterval *interval,
    String *out)
{
    char stack[384];
    size_t len = 0;
    Result r;
    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nDatetimeIntervalFormat(fmt, interval, stack, sizeof(stack), &len);
    if (r == RoboOk) {
        return StringAppend(out, stack, len);
    }
    return r;
}
