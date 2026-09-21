/*
 * RoboI18N date/time formatter (Goal 4.6).
 *
 * Pattern compile once → token list; format walks tokens.
 * Skeleton matching documented in i18n/README.md.
 */

#include "RoboI18N/RoboI18NDateTimeFormat.h"

#include "RoboI18N/RoboCalData.h"
#include "RoboI18N/RoboI18NDisplayNames.h"
#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NLocaleService.h"

#include <string.h>

/* Canonical skeleton field order (CLDR UTS #35 subset). */
static const char kSkelOrder[] = "GyQMwWEdDeEcahHkKmsSAVzZvVxX";

static int IsPatternLetter(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

void I18nDtfmtOptionsInit(I18nDtfmtOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->dateStyle = RoboI18nCalStyleMedium;
    opts->timeStyle = RoboI18nCalStyleMedium;
}

/** Remap hour-cycle letters in @p pattern to match resolved CLDR hc id. */
static void ApplyHourCycleToPattern(char *pattern, size_t cap, const char *hc)
{
    char want = 'H';
    size_t i;
    int inQuote = 0;
    (void)cap;
    if (!pattern || !hc || !hc[0]) {
        return;
    }
    if (strcmp(hc, "h12") == 0) {
        want = 'h';
    } else if (strcmp(hc, "h11") == 0) {
        want = 'K';
    } else if (strcmp(hc, "h24") == 0) {
        want = 'k';
    } else {
        want = 'H'; /* h23 */
    }
    for (i = 0; pattern[i]; i++) {
        char c = pattern[i];
        if (c == '\'') {
            if (pattern[i + 1] == '\'') {
                i++;
                continue;
            }
            inQuote = !inQuote;
            continue;
        }
        if (inQuote) {
            continue;
        }
        if (c == 'h' || c == 'H' || c == 'K' || c == 'k') {
            pattern[i] = want;
        }
    }
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

static Result CompilePattern(I18nDatetimeFormatter *fmt, const char *pattern)
{
    size_t i = 0;
    size_t plen;
    uint16_t tc = 0;

    if (!fmt || !pattern) {
        return RoboInvalidArgument;
    }
    plen = strlen(pattern);
    if (plen == 0 || plen >= RoboI18nDtfmtPatternMax) {
        return RoboInvalidArgument;
    }
    memcpy(fmt->pattern, pattern, plen + 1u);
    if (fmt->options.hourCycle[0]) {
        ApplyHourCycleToPattern(fmt->pattern, sizeof(fmt->pattern),
                                    fmt->options.hourCycle);
        plen = strlen(fmt->pattern);
    }
    pattern = fmt->pattern;
    fmt->tokenCount = 0;
    {
        size_t pack = plen + 1u; /* quoted literals packed after pattern NUL */

    while (i < plen) {
        unsigned char c = (unsigned char)pattern[i];
        I18nDtfmtToken *tok;

        if (tc >= RoboI18nDtfmtTokenMax) {
            return RoboError;
        }
        tok = &fmt->tokens[tc];
        memset(tok, 0, sizeof(*tok));

        if (c == '\'') {
            /* Quoted literal. '' → single quote. */
            size_t j = i + 1u;
            size_t litLen = 0;
            char unpacked[RoboI18nDtfmtPatternMax];

            while (j < plen) {
                if (pattern[j] == '\'') {
                    if (j + 1u < plen && pattern[j + 1u] == '\'') {
                        if (litLen + 1u >= sizeof(unpacked)) {
                            return RoboError;
                        }
                        unpacked[litLen++] = '\'';
                        j += 2u;
                        continue;
                    }
                    break;
                }
                if (litLen + 1u >= sizeof(unpacked)) {
                    return RoboError;
                }
                unpacked[litLen++] = pattern[j++];
            }
            if (pack + litLen >= RoboI18nDtfmtPatternMax) {
                return RoboError;
            }
            memcpy(fmt->pattern + pack, unpacked, litLen);
            tok->isLiteral = 1;
            tok->litOff = (uint16_t)pack;
            tok->litLen = (uint16_t)litLen;
            pack += litLen;
            i = (j < plen && pattern[j] == '\'') ? j + 1u : plen;
            tc++;
            continue;
        }

        if (IsPatternLetter(c)) {
            size_t j = i + 1u;
            while (j < plen && (unsigned char)pattern[j] == c) {
                j++;
            }
            tok->isLiteral = 0;
            tok->field = (uint8_t)c;
            tok->count = (uint8_t)(j - i);
            if (tok->count == 0) {
                return RoboInvalidArgument;
            }
            i = j;
            tc++;
            continue;
        }

        /* Non-letter literal run. */
        {
            size_t j = i + 1u;
            while (j < plen) {
                unsigned char d = (unsigned char)pattern[j];
                if (d == '\'' || IsPatternLetter(d)) {
                    break;
                }
                j++;
            }
            tok->isLiteral = 1;
            tok->litOff = (uint16_t)i;
            tok->litLen = (uint16_t)(j - i);
            i = j;
            tc++;
        }
    }
    } /* pack scope */

    fmt->tokenCount = tc;
    return RoboOk;
}

static size_t FormatOffsetAscii(char *tmp,
                                  size_t cap,
                                  int32_t off,
                                  int withColon,
                                  int withGmt,
                                  int hourWidth)
{
    int sign = off < 0 ? -1 : 1;
    int32_t absOff = off < 0 ? -off : off;
    int hours = (int)(absOff / 3600);
    int mins = (int)((absOff % 3600) / 60);
    size_t n = 0;

    if (!tmp || cap < 12) {
        return 0;
    }
    if (withGmt) {
        tmp[n++] = 'G';
        tmp[n++] = 'M';
        tmp[n++] = 'T';
    }
    tmp[n++] = (char)(sign < 0 ? '-' : '+');
    if (hourWidth >= 2) {
        tmp[n++] = (char)('0' + (hours / 10));
        tmp[n++] = (char)('0' + (hours % 10));
    } else {
        if (hours >= 10) {
            tmp[n++] = (char)('0' + (hours / 10));
        }
        tmp[n++] = (char)('0' + (hours % 10));
    }
    if (withColon) {
        tmp[n++] = ':';
        tmp[n++] = (char)('0' + (mins / 10));
        tmp[n++] = (char)('0' + (mins % 10));
    } else if (hourWidth >= 2) {
        /* Always include minutes for basic ±HHMM forms. */
        tmp[n++] = (char)('0' + (mins / 10));
        tmp[n++] = (char)('0' + (mins % 10));
    }
    tmp[n] = '\0';
    return n;
}

static Result ExpandDatetimeCombo(char *out,
                                           size_t outCap,
                                           const char *combo,
                                           const char *datePat,
                                           const char *timePat)
{
    size_t oi = 0;
    size_t i = 0;
    size_t clen;
    if (!out || !combo || !datePat || !timePat || outCap == 0) {
        return RoboInvalidArgument;
    }
    clen = strlen(combo);
    while (i < clen) {
        if (combo[i] == '{' && i + 2u < clen && combo[i + 2u] == '}') {
            const char *ins = NULL;
            if (combo[i + 1u] == '0') {
                ins = timePat;
            } else if (combo[i + 1u] == '1') {
                ins = datePat;
            }
            if (ins) {
                size_t n = strlen(ins);
                if (oi + n + 1u > outCap) {
                    return RoboError;
                }
                memcpy(out + oi, ins, n);
                oi += n;
                i += 3u;
                continue;
            }
        }
        if (oi + 2u > outCap) {
            return RoboError;
        }
        out[oi++] = combo[i++];
    }
    out[oi] = '\0';
    return RoboOk;
}

/* ---- Skeleton normalize / match ---- */

typedef struct skelField {
    char letter;
    uint8_t count;
} SkelField;

static int SkelOrderIndex(char letter)
{
    const char *p = strchr(kSkelOrder, letter);
    if (!p) {
        return -1;
    }
    return (int)(p - kSkelOrder);
}

static size_t SkelParse(const char *skel, SkelField *out, size_t outCap)
{
    size_t n = 0;
    size_t i = 0;
    size_t len;
    if (!skel || !out) {
        return 0;
    }
    len = strlen(skel);
    while (i < len && n < outCap) {
        char c = skel[i];
        size_t j = i + 1u;
        if (!IsPatternLetter((unsigned char)c)) {
            i++;
            continue;
        }
        while (j < len && skel[j] == c) {
            j++;
        }
        out[n].letter = c;
        out[n].count = (uint8_t)(j - i);
        n++;
        i = j;
    }
    return n;
}

static void SkelNormalize(SkelField *fields, size_t *count)
{
    SkelField tmp[32];
    size_t n;
    size_t i;
    size_t w = 0;
    if (!fields || !count) {
        return;
    }
    n = *count;
    /* Merge duplicates by keeping max count; then sort by kSkelOrder. */
    for (i = 0; i < n; i++) {
        size_t j;
        int found = 0;
        for (j = 0; j < w; j++) {
            if (tmp[j].letter == fields[i].letter) {
                if (fields[i].count > tmp[j].count) {
                    tmp[j].count = fields[i].count;
                }
                found = 1;
                break;
            }
        }
        if (!found && w < 32) {
            tmp[w++] = fields[i];
        }
    }
    /* Insertion sort by order index; unknown letters last. */
    for (i = 1; i < w; i++) {
        SkelField key = tmp[i];
        int ki = SkelOrderIndex(key.letter);
        size_t j = i;
        while (j > 0) {
            int pj = SkelOrderIndex(tmp[j - 1u].letter);
            if (pj <= ki) {
                break;
            }
            tmp[j] = tmp[j - 1u];
            j--;
        }
        tmp[j] = key;
    }
    memcpy(fields, tmp, w * sizeof(SkelField));
    *count = w;
}

static void SkelToString(const SkelField *fields, size_t count, char *out, size_t cap)
{
    size_t i;
    size_t o = 0;
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    for (i = 0; i < count; i++) {
        uint8_t c;
        for (c = 0; c < fields[i].count; c++) {
            if (o + 2u > cap) {
                out[cap - 1u] = '\0';
                return;
            }
            out[o++] = fields[i].letter;
        }
    }
    out[o] = '\0';
}

static int SkelScore(const SkelField *want, size_t wn, const SkelField *have, size_t hn)
{
    int score = 0;
    size_t i;
    size_t j;
    /* Missing requested field: heavy penalty. Extra field: light. Length delta: medium. */
    for (i = 0; i < wn; i++) {
        int found = 0;
        for (j = 0; j < hn; j++) {
            if (have[j].letter == want[i].letter) {
                int d = (int)have[j].count - (int)want[i].count;
                if (d < 0) {
                    d = -d;
                }
                score += d * 4;
                found = 1;
                break;
            }
        }
        if (!found) {
            score += 100;
        }
    }
    for (j = 0; j < hn; j++) {
        int found = 0;
        for (i = 0; i < wn; i++) {
            if (want[i].letter == have[j].letter) {
                found = 1;
                break;
            }
        }
        if (!found) {
            score += 10;
        }
    }
    return score;
}

static const CalAvailableFormat *AvailFindInherited(const I18nLocale *loc,
                                                                const char *calendar,
                                                                const char *skeleton)
{
    I18nLocale cur;
    int guard = 0;
    char tag[64];
    const CalAvailableFormat *row;

    if (!loc || !calendar || !skeleton) {
        return NULL;
    }
    cur = *loc;
    while (guard++ < 16) {
        FormatLocaleTag(&cur, tag, sizeof(tag));
        row = CalAvailableFormatFind(tag, calendar, skeleton);
        if (row) {
            return row;
        }
        row = CalAvailableFormatFind(cur.language, calendar, skeleton);
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
    return CalAvailableFormatFind("en", calendar, skeleton);
}

static Result ResolveSkeletonPattern(const I18nLocale *locale,
                                              const I18nCalendar *calendar,
                                              const char *skeleton,
                                              const char **outPattern)
{
    SkelField want[32];
    size_t wn;
    char norm[RoboI18nDtfmtSkeletonMax];
    const CalAvailableFormat *row;
    const char *cid;
    I18nLocale cur;
    int guard = 0;
    char tag[64];
    int bestScore = 0x7fffffff;
    const char *bestPat = NULL;

    if (!locale || !calendar || !skeleton || !outPattern) {
        return RoboInvalidArgument;
    }
    *outPattern = NULL;
    cid = calendar->id;
    wn = SkelParse(skeleton, want, 32);
    if (wn == 0) {
        return RoboInvalidArgument;
    }
    SkelNormalize(want, &wn);
    SkelToString(want, wn, norm, sizeof(norm));

    row = AvailFindInherited(locale, cid, norm);
    if (!row) {
        row = AvailFindInherited(locale, cid, skeleton);
    }
    if (row) {
        *outPattern = CalStr(row->pattern);
        return RoboOk;
    }

    /* Best-match over availableFormats for locale inheritance chain. */
    cur = *locale;
    while (guard++ < 16) {
        size_t count = 0;
        const CalAvailableFormat *begin;
        size_t i;
        FormatLocaleTag(&cur, tag, sizeof(tag));
        begin = CalAvailableFormatsBegin(tag, cid, &count);
        if (!begin || count == 0) {
            begin = CalAvailableFormatsBegin(cur.language, cid, &count);
        }
        if (begin) {
            for (i = 0; i < count; i++) {
                SkelField have[32];
                size_t hn;
                int sc;
                const char *sk = CalStr(begin[i].skeleton);
                hn = SkelParse(sk, have, 32);
                SkelNormalize(have, &hn);
                sc = SkelScore(want, wn, have, hn);
                if (sc < bestScore) {
                    bestScore = sc;
                    bestPat = CalStr(begin[i].pattern);
                }
            }
        }
        {
            I18nLocale parent;
            if (I18nLocaleDataParent(&cur, &parent) != RoboOk) {
                break;
            }
            cur = parent;
        }
    }
    if (!bestPat) {
        size_t count = 0;
        const CalAvailableFormat *begin =
            CalAvailableFormatsBegin("en", cid, &count);
        size_t i;
        if (begin) {
            for (i = 0; i < count; i++) {
                SkelField have[32];
                size_t hn;
                int sc;
                const char *sk = CalStr(begin[i].skeleton);
                hn = SkelParse(sk, have, 32);
                SkelNormalize(have, &hn);
                sc = SkelScore(want, wn, have, hn);
                if (sc < bestScore) {
                    bestScore = sc;
                    bestPat = CalStr(begin[i].pattern);
                }
            }
        }
    }
    if (!bestPat || bestScore >= 100) {
        return RoboNotFound;
    }
    *outPattern = bestPat;
    return RoboOk;
}

Result I18nDatetimeResolveSkeleton(const I18nLocale *locale,
                                                  const I18nCalendar *calendar,
                                                  const char *skeleton,
                                                  const char **outPattern)
{
    I18nCalendar cal;
    if (!calendar) {
        if (I18nCalendarFromKind(RoboI18nCalGregorian, &cal) != RoboOk) {
            return RoboInvalidArgument;
        }
        calendar = &cal;
    }
    return ResolveSkeletonPattern(locale, calendar, skeleton, outPattern);
}

static Result InitCommon(I18nDatetimeFormatter *fmt,
                                 const I18nLocale *locale,
                                 const I18nCalendar *calendar,
                                 const I18nTimezone *tz,
                                 const I18nDtfmtOptions *opts)
{
    I18nNumberOptions nopts;
    Result r;
    char calId[32];
    char tzId[RoboI18nTzIdMax + 1u];
    char hc[8];

    if (!fmt || !locale) {
        return RoboInvalidArgument;
    }
    memset(fmt, 0, sizeof(*fmt));
    fmt->locale = *locale;
    if (opts) {
        fmt->options = *opts;
    } else {
        I18nDtfmtOptionsInit(&fmt->options);
    }

    if (calendar) {
        fmt->calendar = *calendar;
    } else {
        I18nLocaleSettings st;
        calId[0] = '\0';
        if (I18nLocaleResolve(locale, NULL, &st) == RoboOk &&
            st.calendarSrc == RoboI18nSrcExtension && st.calendar[0]) {
            size_t n = strlen(st.calendar);
            if (n >= sizeof(calId)) {
                n = sizeof(calId) - 1u;
            }
            memcpy(calId, st.calendar, n);
            calId[n] = '\0';
        }
        if (!calId[0] ||
            I18nCalendarFromId(calId, strlen(calId), &fmt->calendar) != RoboOk ||
            !I18nCalendarHasConversion(&fmt->calendar)) {
            r = I18nCalendarFromKind(RoboI18nCalGregorian, &fmt->calendar);
            if (r != RoboOk) {
                return r;
            }
        }
    }
    if (!I18nCalendarHasConversion(&fmt->calendar)) {
        return RoboNotSupported;
    }

    if (tz) {
        fmt->timezone = *tz;
    } else {
        tzId[0] = '\0';
        (void)I18nLocaleResolveTimezone(locale, NULL, tzId, sizeof(tzId));
        if (!tzId[0] ||
            I18nTimezoneFromId(tzId, strlen(tzId), &fmt->timezone) != RoboOk) {
            r = I18nTimezoneUtc(&fmt->timezone);
            if (r != RoboOk) {
                return r;
            }
        }
    }

    I18nNumberOptionsInit(&nopts);
    nopts.useGrouping = 0;
    nopts.minIntegerDigits = 1;
    nopts.minFractionDigits = 0;
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

    hc[0] = '\0';
    if (I18nLocaleResolveHourCycle(
            locale, fmt->options.hourCycle[0] ? fmt->options.hourCycle : NULL, hc,
            sizeof(hc)) == RoboOk &&
        hc[0]) {
        size_t n = strlen(hc);
        if (n < sizeof(fmt->options.hourCycle)) {
            memcpy(fmt->options.hourCycle, hc, n + 1u);
        }
    }
    return RoboOk;
}

Result I18nDatetimeFormatterInitPattern(
    I18nDatetimeFormatter *fmt,
    const I18nLocale *locale,
    const I18nCalendar *calendar,
    const I18nTimezone *tz,
    const char *pattern,
    const I18nDtfmtOptions *opts)
{
    Result r = InitCommon(fmt, locale, calendar, tz, opts);
    if (r != RoboOk) {
        return r;
    }
    r = CompilePattern(fmt, pattern);
    if (r != RoboOk) {
        I18nDatetimeFormatterDestroy(fmt);
        return r;
    }
    fmt->ready = 1;
    return RoboOk;
}

Result I18nDatetimeFormatterInit(I18nDatetimeFormatter *fmt,
                                                const I18nLocale *locale,
                                                const I18nCalendar *calendar,
                                                const I18nTimezone *tz,
                                                I18nDtfmtKind kind,
                                                const I18nDtfmtOptions *opts)
{
    I18nDtfmtOptions localOpts;
    const char *datePat = NULL;
    const char *timePat = NULL;
    const char *combo = NULL;
    char combined[RoboI18nDtfmtPatternMax];
    Result r;
    I18nCalendar tmpCal;
    const I18nCalendar *cal;

    if (opts) {
        localOpts = *opts;
    } else {
        I18nDtfmtOptionsInit(&localOpts);
    }

    cal = calendar;
    if (!cal) {
        r = I18nCalendarFromKind(RoboI18nCalGregorian, &tmpCal);
        if (r != RoboOk) {
            return r;
        }
        cal = &tmpCal;
    }

    if (kind == RoboI18nDtfmtDate || kind == RoboI18nDtfmtDatetime) {
        r = I18nCalendarDatePattern(locale, cal, localOpts.dateStyle, &datePat);
        if (r != RoboOk) {
            return r;
        }
    }
    if (kind == RoboI18nDtfmtTime || kind == RoboI18nDtfmtDatetime) {
        r = I18nCalendarTimePattern(locale, cal, localOpts.timeStyle, &timePat);
        if (r != RoboOk) {
            return r;
        }
    }
    if (kind == RoboI18nDtfmtDate) {
        return I18nDatetimeFormatterInitPattern(fmt, locale, calendar, tz, datePat,
                                                         &localOpts);
    }
    if (kind == RoboI18nDtfmtTime) {
        return I18nDatetimeFormatterInitPattern(fmt, locale, calendar, tz, timePat,
                                                         &localOpts);
    }
    if (kind != RoboI18nDtfmtDatetime) {
        return RoboInvalidArgument;
    }
    r = I18nCalendarDatetimePattern(locale, cal, localOpts.dateStyle, &combo);
    if (r != RoboOk) {
        return r;
    }
    r = ExpandDatetimeCombo(combined, sizeof(combined), combo, datePat, timePat);
    if (r != RoboOk) {
        return r;
    }
    return I18nDatetimeFormatterInitPattern(fmt, locale, calendar, tz, combined,
                                                     &localOpts);
}

Result I18nDatetimeFormatterInitSkeleton(
    I18nDatetimeFormatter *fmt,
    const I18nLocale *locale,
    const I18nCalendar *calendar,
    const I18nTimezone *tz,
    const char *skeleton,
    const I18nDtfmtOptions *opts)
{
    const char *pattern = NULL;
    I18nCalendar tmp;
    const I18nCalendar *cal = calendar;
    Result r;

    if (!cal) {
        r = I18nCalendarFromKind(RoboI18nCalGregorian, &tmp);
        if (r != RoboOk) {
            return r;
        }
        cal = &tmp;
    }
    r = ResolveSkeletonPattern(locale, cal, skeleton, &pattern);
    if (r != RoboOk) {
        return r;
    }
    return I18nDatetimeFormatterInitPattern(fmt, locale, calendar, tz, pattern, opts);
}

void I18nDatetimeFormatterDestroy(I18nDatetimeFormatter *fmt)
{
    if (!fmt) {
        return;
    }
    I18nNumberFormatterDestroy(&fmt->number);
    memset(fmt, 0, sizeof(*fmt));
}

/* ---- Field emission ---- */

typedef struct fmtCtx {
    const I18nDatetimeFormatter *fmt;
    I18nCalendarFields fields;
    I18nDate gregorian;
    I18nTime time;
    I18nWeekday weekday;
    int32_t offsetSeconds; /* known zone offset for display; 0 if UTC */
    const I18nInstant *instant; /* non-NULL when formatting an instant */
    int haveTime;
    int haveDate;
} FmtCtx;

static Result EmitNumber(const I18nDatetimeFormatter *fmt,
                                 int64_t value,
                                 int minDigits,
                                 char *buf,
                                 size_t bufCap,
                                 size_t *pos,
                                 int writing)
{
    I18nNumberFormatter nf = fmt->number;
    char tmp[48];
    size_t len = 0;
    Result r;

    nf.options.minIntegerDigits = minDigits < 1 ? 1 : minDigits;
    if (nf.options.minIntegerDigits > 16) {
        nf.options.minIntegerDigits = 16;
    }
    nf.options.useGrouping = 0;
    nf.options.maxFractionDigits = 0;
    nf.options.minFractionDigits = 0;
    r = I18nNumberFormatI64(&nf, value, tmp, sizeof(tmp), &len);
    if (r != RoboOk) {
        return r;
    }
    return AppendBytes(buf, bufCap, pos, tmp, len, writing);
}

static Result EmitText(char *buf,
                               size_t bufCap,
                               size_t *pos,
                               const char *s,
                               int writing)
{
    if (!s) {
        return RoboNotFound;
    }
    return AppendBytes(buf, bufCap, pos, s, strlen(s), writing);
}

static Result EmitZone(const FmtCtx *ctx,
                               char field,
                               uint8_t count,
                               char *buf,
                               size_t bufCap,
                               size_t *pos,
                               int writing)
{
    int32_t off = ctx->offsetSeconds;
    char tmp[128];
    size_t n = 0;
    Result r;
    I18nTimezoneNameStyle style;
    const I18nInstant *inst = ctx->instant;

    /* Localized names via Goal 4.15 display layer. */
    if (field == 'z') {
        style = count >= 4 ? RoboI18nTzDispSpecificLong : RoboI18nTzDispSpecificShort;
        r = I18nTimezoneDisplayNameAt(&ctx->fmt->timezone, inst, style, &ctx->fmt->locale,
                                               NULL, tmp, sizeof(tmp), &n);
        if (r == RoboOk) {
            return AppendBytes(buf, bufCap, pos, tmp, n, writing);
        }
    } else if (field == 'v') {
        style = count >= 4 ? RoboI18nTzDispLongGeneric : RoboI18nTzDispShortGeneric;
        r = I18nTimezoneDisplayNameAt(&ctx->fmt->timezone, inst, style, &ctx->fmt->locale,
                                               NULL, tmp, sizeof(tmp), &n);
        if (r == RoboOk) {
            return AppendBytes(buf, bufCap, pos, tmp, n, writing);
        }
    } else if (field == 'V') {
        if (count >= 4) {
            style = RoboI18nTzDispExemplarCity;
        } else if (count == 1) {
            style = RoboI18nTzDispIdentifier;
        } else {
            style = RoboI18nTzDispExemplarCity;
        }
        r = I18nTimezoneDisplayNameAt(&ctx->fmt->timezone, inst, style, &ctx->fmt->locale,
                                               NULL, tmp, sizeof(tmp), &n);
        if (r == RoboOk) {
            return AppendBytes(buf, bufCap, pos, tmp, n, writing);
        }
    } else if (field == 'O') {
        r = I18nTimezoneDisplayNameAt(&ctx->fmt->timezone, inst, RoboI18nTzDispGmt,
                                               &ctx->fmt->locale, NULL, tmp, sizeof(tmp), &n);
        if (r == RoboOk) {
            return AppendBytes(buf, bufCap, pos, tmp, n, writing);
        }
    }

    if (field == 'X' && off == 0) {
        return AppendBytes(buf, bufCap, pos, "Z", 1u, writing);
    }
    if (field == 'Z' && count < 5) {
        n = FormatOffsetAscii(tmp, sizeof(tmp), off, 0, 0, 2);
    } else if (field == 'X' || field == 'x') {
        if (count == 1) {
            int sign = off < 0 ? -1 : 1;
            int hours = (int)((off < 0 ? -off : off) / 3600);
            tmp[0] = (char)(sign < 0 ? '-' : '+');
            tmp[1] = (char)('0' + (hours / 10));
            tmp[2] = (char)('0' + (hours % 10));
            tmp[3] = '\0';
            n = 3;
        } else if (count == 2 || count == 4) {
            n = FormatOffsetAscii(tmp, sizeof(tmp), off, 0, 0, 2);
        } else {
            n = FormatOffsetAscii(tmp, sizeof(tmp), off, 1, 0, 2);
        }
    } else if (field == 'Z' && count >= 5) {
        n = FormatOffsetAscii(tmp, sizeof(tmp), off, 1, 0, 2);
    } else {
        /* Fallback GMT±HH:MM ASCII if localized path failed. */
        n = FormatOffsetAscii(tmp, sizeof(tmp), off, 1, 1, 2);
    }
    if (n == 0) {
        return RoboError;
    }
    return AppendBytes(buf, bufCap, pos, tmp, n, writing);
}

static int hour12(uint8_t hour24)
{
    int h = (int)(hour24 % 12u);
    return h == 0 ? 12 : h;
}

static Result EmitField(const FmtCtx *ctx,
                                char field,
                                uint8_t count,
                                char *buf,
                                size_t bufCap,
                                size_t *pos,
                                int writing)
{
    const I18nDatetimeFormatter *fmt = ctx->fmt;
    const char *name = NULL;
    Result r;
    I18nCalNameWidth width;

    switch (field) {
    case 'G':
        width = count >= 5   ? RoboI18nCalNameNarrow
                : count >= 4 ? RoboI18nCalNameWide
                             : RoboI18nCalNameAbbreviated;
        r = I18nCalendarEraName(&fmt->locale, &fmt->calendar, ctx->fields.eraIndex, width,
                                        &name);
        if (r != RoboOk) {
            return r;
        }
        return EmitText(buf, bufCap, pos, name, writing);

    case 'y':
    case 'Y':
    case 'u':
        if (!ctx->haveDate) {
            return RoboInvalidArgument;
        }
        {
            int64_t year = ctx->fields.year;
            int minD = count == 2 ? 2 : (count >= 4 ? 4 : count);
            if (count == 2) {
                year = year % 100;
                if (year < 0) {
                    year = -year;
                }
            }
            return EmitNumber(fmt, year, minD, buf, bufCap, pos, writing);
        }

    case 'M':
    case 'L':
        if (!ctx->haveDate) {
            return RoboInvalidArgument;
        }
        if (count == 1 || count == 2) {
            return EmitNumber(fmt, (int64_t)ctx->fields.month, count, buf, bufCap, pos, writing);
        }
        width = count >= 5   ? RoboI18nCalNameNarrow
                : count == 4 ? RoboI18nCalNameWide
                             : RoboI18nCalNameAbbreviated;
        r = I18nCalendarMonthName(&fmt->locale, &fmt->calendar, ctx->fields.month, width,
                                          &name);
        if (r != RoboOk) {
            return r;
        }
        return EmitText(buf, bufCap, pos, name, writing);

    case 'd':
        if (!ctx->haveDate) {
            return RoboInvalidArgument;
        }
        return EmitNumber(fmt, (int64_t)ctx->fields.day, count >= 2 ? 2 : 1, buf, bufCap, pos,
                           writing);

    case 'E':
    case 'e':
    case 'c':
        if (!ctx->haveDate) {
            return RoboInvalidArgument;
        }
        if ((field == 'e' || field == 'c') && count <= 2) {
            /* Local day-of-week number — use ISO weekday for now. */
            return EmitNumber(fmt, (int64_t)ctx->weekday, count, buf, bufCap, pos, writing);
        }
        width = count >= 5   ? RoboI18nCalNameNarrow
                : count >= 4 ? RoboI18nCalNameWide
                             : RoboI18nCalNameAbbreviated;
        r = I18nCalendarWeekdayName(&fmt->locale, &fmt->calendar, ctx->weekday, width,
                                            &name);
        if (r != RoboOk) {
            return r;
        }
        return EmitText(buf, bufCap, pos, name, writing);

    case 'a':
    case 'b':
    case 'B':
        if (!ctx->haveTime) {
            return RoboInvalidArgument;
        }
        {
            const char *pid = ctx->time.hour < 12 ? "am" : "pm";
            width = count >= 5   ? RoboI18nCalNameNarrow
                    : count >= 4 ? RoboI18nCalNameWide
                                 : RoboI18nCalNameAbbreviated;
            r = I18nCalendarDayPeriodName(&fmt->locale, &fmt->calendar, pid, width, &name);
            if (r != RoboOk) {
                return r;
            }
            return EmitText(buf, bufCap, pos, name, writing);
        }

    case 'h':
        if (!ctx->haveTime) {
            return RoboInvalidArgument;
        }
        return EmitNumber(fmt, (int64_t)hour12(ctx->time.hour), count >= 2 ? 2 : 1, buf, bufCap,
                           pos, writing);

    case 'H':
        if (!ctx->haveTime) {
            return RoboInvalidArgument;
        }
        return EmitNumber(fmt, (int64_t)ctx->time.hour, count >= 2 ? 2 : 1, buf, bufCap, pos,
                           writing);

    case 'K':
        if (!ctx->haveTime) {
            return RoboInvalidArgument;
        }
        return EmitNumber(fmt, (int64_t)(ctx->time.hour % 12u), count >= 2 ? 2 : 1, buf, bufCap,
                           pos, writing);

    case 'k':
        if (!ctx->haveTime) {
            return RoboInvalidArgument;
        }
        {
            int k = ctx->time.hour == 0 ? 24 : (int)ctx->time.hour;
            return EmitNumber(fmt, (int64_t)k, count >= 2 ? 2 : 1, buf, bufCap, pos, writing);
        }

    case 'm':
        if (!ctx->haveTime) {
            return RoboInvalidArgument;
        }
        return EmitNumber(fmt, (int64_t)ctx->time.minute, count >= 2 ? 2 : 1, buf, bufCap, pos,
                           writing);

    case 's':
        if (!ctx->haveTime) {
            return RoboInvalidArgument;
        }
        return EmitNumber(fmt, (int64_t)ctx->time.second, count >= 2 ? 2 : 1, buf, bufCap, pos,
                           writing);

    case 'S':
        if (!ctx->haveTime) {
            return RoboInvalidArgument;
        }
        {
            uint8_t want = count > 9 ? 9 : count;
            uint32_t div = 1u;
            uint8_t i;
            uint32_t v;
            for (i = 0; i < (uint8_t)(9u - want); i++) {
                div *= 10u;
            }
            v = ctx->time.nano / div;
            return EmitNumber(fmt, (int64_t)v, (int)want, buf, bufCap, pos, writing);
        }

    case 'z':
    case 'Z':
    case 'O':
    case 'v':
    case 'V':
    case 'x':
    case 'X':
        return EmitZone(ctx, field, count, buf, bufCap, pos, writing);

    default:
        /* Unsupported pattern letter: emit as-is (letter × count) for resilience. */
        {
            char tmp[16];
            uint8_t i;
            uint8_t n = count > 15 ? 15 : count;
            for (i = 0; i < n; i++) {
                tmp[i] = field;
            }
            return AppendBytes(buf, bufCap, pos, tmp, n, writing);
        }
    }
}

static Result FormatCtx(const FmtCtx *ctx, char *buf, size_t bufCap, size_t *outLen)
{
    size_t pos = 0;
    size_t i;
    int writing = (buf != NULL && bufCap > 0);
    Result r;

    if (!ctx || !ctx->fmt || !ctx->fmt->ready || !outLen) {
        return RoboInvalidArgument;
    }

    for (i = 0; i < ctx->fmt->tokenCount; i++) {
        const I18nDtfmtToken *tok = &ctx->fmt->tokens[i];
        if (tok->isLiteral) {
            r = AppendBytes(buf, bufCap, &pos, ctx->fmt->pattern + tok->litOff, tok->litLen,
                             writing);
            if (r != RoboOk) {
                *outLen = pos;
                return r;
            }
        } else {
            r = EmitField(ctx, (char)tok->field, tok->count, buf, bufCap, &pos, writing);
            if (r != RoboOk) {
                *outLen = pos;
                return r;
            }
        }
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

static Result PrepareFromDatetime(const I18nDatetimeFormatter *fmt,
                                           const I18nDatetime *dt,
                                           int needDate,
                                           int needTime,
                                           FmtCtx *ctx)
{
    Result r;
    if (!fmt || !fmt->ready || !dt || !ctx) {
        return RoboInvalidArgument;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->fmt = fmt;
    ctx->haveDate = needDate;
    ctx->haveTime = needTime;
    ctx->time = dt->time;
    ctx->gregorian = dt->date;
    ctx->offsetSeconds = fmt->timezone.offsetSeconds;
    if (fmt->timezone.kind == RoboI18nTzUtc) {
        ctx->offsetSeconds = 0;
    }

    if (needDate) {
        if (!I18nDateIsValid(&dt->date)) {
            return RoboInvalidArgument;
        }
        r = I18nCalendarFromGregorian(&fmt->calendar, &dt->date, &ctx->fields);
        if (r != RoboOk) {
            return r;
        }
        r = I18nDateDayOfWeek(&dt->date, &ctx->weekday);
        if (r != RoboOk) {
            return r;
        }
    }
    if (needTime) {
        if (!I18nTimeIsValid(&dt->time)) {
            return RoboInvalidArgument;
        }
    }
    return RoboOk;
}

Result I18nDatetimeFormatDate(const I18nDatetimeFormatter *fmt,
                                             const I18nDate *date,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen)
{
    I18nDatetime dt;
    FmtCtx ctx;
    Result r;
    if (!date) {
        return RoboInvalidArgument;
    }
    memset(&dt, 0, sizeof(dt));
    dt.date = *date;
    r = PrepareFromDatetime(fmt, &dt, 1, 0, &ctx);
    if (r != RoboOk) {
        return r;
    }
    return FormatCtx(&ctx, buf, bufCap, outLen);
}

Result I18nDatetimeFormatTime(const I18nDatetimeFormatter *fmt,
                                             const I18nTime *time,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen)
{
    I18nDatetime dt;
    FmtCtx ctx;
    Result r;
    if (!time) {
        return RoboInvalidArgument;
    }
    memset(&dt, 0, sizeof(dt));
    dt.time = *time;
    r = PrepareFromDatetime(fmt, &dt, 0, 1, &ctx);
    if (r != RoboOk) {
        return r;
    }
    return FormatCtx(&ctx, buf, bufCap, outLen);
}

Result I18nDatetimeFormatDatetime(const I18nDatetimeFormatter *fmt,
                                                 const I18nDatetime *dt,
                                                 char *buf,
                                                 size_t bufCap,
                                                 size_t *outLen)
{
    FmtCtx ctx;
    Result r = PrepareFromDatetime(fmt, dt, 1, 1, &ctx);
    if (r != RoboOk) {
        return r;
    }
    return FormatCtx(&ctx, buf, bufCap, outLen);
}

Result I18nDatetimeFormatInstant(const I18nDatetimeFormatter *fmt,
                                                const I18nInstant *instant,
                                                char *buf,
                                                size_t bufCap,
                                                size_t *outLen)
{
    I18nDatetime local;
    I18nTzResolve resolve = RoboI18nTzUnique;
    FmtCtx ctx;
    Result r;
    int32_t off = 0;

    if (!fmt || !fmt->ready || !instant) {
        return RoboInvalidArgument;
    }
    r = I18nTimezoneToLocal(&fmt->timezone, instant, &local, &resolve);
    if (r != RoboOk) {
        return r;
    }
    if (resolve != RoboI18nTzUnique) {
        /* Should not happen for UTC/FIXED; refuse ambiguous without policy. */
        return RoboNotSupported;
    }
    r = I18nTimezoneOffsetAt(&fmt->timezone, instant, &off);
    if (r != RoboOk) {
        return r;
    }
    r = PrepareFromDatetime(fmt, &local, 1, 1, &ctx);
    if (r != RoboOk) {
        return r;
    }
    ctx.offsetSeconds = off;
    ctx.instant = instant;
    return FormatCtx(&ctx, buf, bufCap, outLen);
}

Result I18nDatetimeFormatToString(const I18nDatetimeFormatter *fmt,
                                                  const I18nDatetime *dt,
                                                  String *out)
{
    char stack[384];
    size_t len = 0;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nDatetimeFormatDatetime(fmt, dt, stack, sizeof(stack), &len);
    if (r == RoboOk) {
        return StringAppend(out, stack, len);
    }
    return r;
}
