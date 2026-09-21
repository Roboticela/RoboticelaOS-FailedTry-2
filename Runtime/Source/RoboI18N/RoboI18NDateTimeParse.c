/*
 * RoboI18N date/time parsing (Goal 4.23).
 *
 * Pattern-driven reverse of Goal 4.6 formatting. Reuses compiled formatter
 * tokens and CLDR name/digit tables — no runtime CLDR XML/JSON parsing.
 */

#include "RoboI18N/RoboI18NDateTimeParse.h"

#include "RoboI18N/RoboCalData.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <stdint.h>
#include <string.h>

typedef struct parseState {
    const I18nDatetimeParser *parser;
    const char *text;
    size_t len;
    size_t at;

    int haveYear;
    int haveMonth;
    int haveDay;
    int haveHour;
    int haveMinute;
    int haveSecond;
    int haveNano;
    int haveWeekday;
    int haveEra;
    int haveAmpm;
    int ampmIsPm;
    int hourStyle; /* 'h','H','K','k' */
    int32_t hourRaw;
    int32_t year;
    int32_t month;
    int32_t day;
    int32_t eraIndex;
    int32_t weekday; /* ISO 1..7 */
    int32_t minute;
    int32_t second;
    uint32_t nano;
    int haveOffset;
    int haveTzId;
    int32_t offsetSeconds;
    char tzId[RoboI18nTzIdMax + 1u];
} ParseState;

void I18nDtparseOptionsInit(I18nDtparseOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->mode = RoboI18nParseStrict;
    opts->dateStyle = RoboI18nCalStyleMedium;
    opts->timeStyle = RoboI18nCalStyleMedium;
    opts->allowLeadingWs = 1;
    opts->allowTrailingWs = 1;
    opts->requireFull = 1;
    opts->resolveInstant = 0;
    opts->disambiguation = RoboI18nDtparseDisambigReject;
}

void I18nDtparseResultClear(I18nDtparseResult *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->error = RoboI18nDtparseErrNone;
    out->tzResolve = RoboI18nTzUnique;
}

const char *I18nDtparseErrString(I18nDtparseErr err)
{
    switch (err) {
    case RoboI18nDtparseErrNone:
        return "ok";
    case RoboI18nDtparseErrEmpty:
        return "empty";
    case RoboI18nDtparseErrInvalidUtf8:
        return "invalid_utf8";
    case RoboI18nDtparseErrUnexpectedChar:
        return "unexpected_char";
    case RoboI18nDtparseErrTrailing:
        return "trailing";
    case RoboI18nDtparseErrInvalidDate:
        return "invalid_date";
    case RoboI18nDtparseErrInvalidTime:
        return "invalid_time";
    case RoboI18nDtparseErrInvalidField:
        return "invalid_field";
    case RoboI18nDtparseErrInvalidMonthName:
        return "invalid_month_name";
    case RoboI18nDtparseErrInvalidWeekdayName:
        return "invalid_weekday_name";
    case RoboI18nDtparseErrInvalidEra:
        return "invalid_era";
    case RoboI18nDtparseErrInvalidDayPeriod:
        return "invalid_day_period";
    case RoboI18nDtparseErrInvalidTimezone:
        return "invalid_timezone";
    case RoboI18nDtparseErrAmbiguousTimezone:
        return "ambiguous_timezone";
    case RoboI18nDtparseErrAmbiguousLocal:
        return "ambiguous_local";
    case RoboI18nDtparseErrNonexistentLocal:
        return "nonexistent_local";
    case RoboI18nDtparseErrMissingTimezone:
        return "missing_timezone";
    case RoboI18nDtparseErrOverflow:
        return "overflow";
    case RoboI18nDtparseErrUnsupportedCalendar:
        return "unsupported_calendar";
    case RoboI18nDtparseErrUnsupportedLocale:
        return "unsupported_locale";
    case RoboI18nDtparseErrInputTooLong:
        return "input_too_long";
    case RoboI18nDtparseErrPattern:
        return "pattern";
    default:
        return "unknown";
    }
}

static int BuildDigitTable(I18nDatetimeParser *parser)
{
    const char *g;
    size_t glen;
    size_t off = 0u;
    int d;

    for (d = 0; d < 10; d++) {
        parser->digitUtf8[d] = NULL;
        parser->digitLen[d] = 0u;
    }
    g = parser->fmt.number.digitGlyphs;
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

static void InferHourCycleFromPattern(const char *pattern, char out[8])
{
    size_t i;
    int inQuote = 0;
    char seen = 0;

    out[0] = '\0';
    if (!pattern) {
        return;
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
            seen = c;
            break;
        }
    }
    if (seen == 'h') {
        memcpy(out, "h12", 4);
    } else if (seen == 'K') {
        memcpy(out, "h11", 4);
    } else if (seen == 'k') {
        memcpy(out, "h24", 4);
    } else if (seen == 'H') {
        memcpy(out, "h23", 4);
    }
}

static void CopyOptsToDtfmt(const I18nDtparseOptions *opts,
                               I18nDtfmtOptions *fopts)
{
    I18nDtfmtOptionsInit(fopts);
    if (!opts) {
        return;
    }
    fopts->dateStyle = opts->dateStyle;
    fopts->timeStyle = opts->timeStyle;
    if (opts->numberingSystem[0]) {
        memcpy(fopts->numberingSystem, opts->numberingSystem,
               sizeof(fopts->numberingSystem));
    }
    if (opts->hourCycle[0]) {
        memcpy(fopts->hourCycle, opts->hourCycle, sizeof(fopts->hourCycle));
    }
}

static Result FinishParserInit(I18nDatetimeParser *parser,
                                        const I18nDtparseOptions *opts,
                                        I18nDtfmtKind kind,
                                        int hasResolveTz)
{
    if (opts) {
        parser->options = *opts;
    } else {
        I18nDtparseOptionsInit(&parser->options);
    }
    parser->kind = kind;
    parser->hasResolveTz = hasResolveTz ? 1u : 0u;
    if (!BuildDigitTable(parser)) {
        I18nDatetimeFormatterDestroy(&parser->fmt);
        return RoboError;
    }
    parser->ready = 1u;
    return RoboOk;
}

Result I18nDatetimeParserInit(I18nDatetimeParser *parser,
                                             const I18nLocale *locale,
                                             const I18nCalendar *calendar,
                                             const I18nTimezone *resolveTz,
                                             I18nDtfmtKind kind,
                                             const I18nDtparseOptions *opts)
{
    I18nDtfmtOptions fopts;
    Result r;

    if (!parser || !locale) {
        return RoboInvalidArgument;
    }
    memset(parser, 0, sizeof(*parser));
    CopyOptsToDtfmt(opts, &fopts);
    r = I18nDatetimeFormatterInit(&parser->fmt, locale, calendar, resolveTz, kind,
                                          &fopts);
    if (r != RoboOk) {
        return r;
    }
    return FinishParserInit(parser, opts, kind, resolveTz != NULL);
}

Result I18nDatetimeParserInitPattern(
    I18nDatetimeParser *parser,
    const I18nLocale *locale,
    const I18nCalendar *calendar,
    const I18nTimezone *resolveTz,
    const char *pattern,
    const I18nDtparseOptions *opts)
{
    I18nDtfmtOptions fopts;
    Result r;

    if (!parser || !locale || !pattern) {
        return RoboInvalidArgument;
    }
    memset(parser, 0, sizeof(*parser));
    CopyOptsToDtfmt(opts, &fopts);
    /* Preserve explicit pattern hour letters (avoid locale hc remapping H→h). */
    if (!fopts.hourCycle[0]) {
        InferHourCycleFromPattern(pattern, fopts.hourCycle);
    }
    r = I18nDatetimeFormatterInitPattern(&parser->fmt, locale, calendar, resolveTz,
                                                  pattern, &fopts);
    if (r != RoboOk) {
        return r;
    }
    return FinishParserInit(parser, opts, RoboI18nDtfmtDatetime, resolveTz != NULL);
}

Result I18nDatetimeParserInitSkeleton(
    I18nDatetimeParser *parser,
    const I18nLocale *locale,
    const I18nCalendar *calendar,
    const I18nTimezone *resolveTz,
    const char *skeleton,
    const I18nDtparseOptions *opts)
{
    I18nDtfmtOptions fopts;
    Result r;

    if (!parser || !locale || !skeleton) {
        return RoboInvalidArgument;
    }
    memset(parser, 0, sizeof(*parser));
    CopyOptsToDtfmt(opts, &fopts);
    if (!fopts.hourCycle[0]) {
        const char *pat = NULL;
        if (I18nDatetimeResolveSkeleton(locale, calendar, skeleton, &pat) == RoboOk &&
            pat) {
            InferHourCycleFromPattern(pat, fopts.hourCycle);
        }
    }
    r = I18nDatetimeFormatterInitSkeleton(&parser->fmt, locale, calendar, resolveTz,
                                                   skeleton, &fopts);
    if (r != RoboOk) {
        return r;
    }
    return FinishParserInit(parser, opts, RoboI18nDtfmtDatetime, resolveTz != NULL);
}

void I18nDatetimeParserDestroy(I18nDatetimeParser *parser)
{
    if (!parser) {
        return;
    }
    I18nDatetimeFormatterDestroy(&parser->fmt);
    memset(parser, 0, sizeof(*parser));
}

/* ---- UTF-8 / whitespace helpers ---- */

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

static int MatchDigit(const I18nDatetimeParser *p,
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
    if (text[at] >= '0' && text[at] <= '9') {
        *outD = (int)(text[at] - '0');
        *outNext = at + 1u;
        return 1;
    }
    return 0;
}

static int Utf8ValidSpan(const char *text, size_t len, size_t *badOff)
{
    size_t at = 0;
    while (at < len) {
        Uchar32 cp = 0;
        size_t next = at;
        if (UnicodeUtf8Next(text, len, at, RoboUnicodeStrict, &cp, &next) !=
            RoboUnicodeOk) {
            if (badOff) {
                *badOff = at;
            }
            return 0;
        }
        at = next;
    }
    return 1;
}

static int PrefixEq(const char *text, size_t len, size_t at, const char *lit, size_t litLen)
{
    if (!lit || litLen == 0) {
        return 1;
    }
    if (at + litLen > len) {
        return 0;
    }
    return memcmp(text + at, lit, litLen) == 0;
}

static int AsciiIeq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
    }
    return a == b;
}

static int MatchAsciiCi(const char *text, size_t len, size_t at, const char *lit, size_t *next)
{
    size_t i;
    size_t n = strlen(lit);
    if (at + n > len) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (!AsciiIeq(text[at + i], lit[i])) {
            return 0;
        }
    }
    *next = at + n;
    return 1;
}

/* Read unsigned decimal with localized digits. maxDigits caps length.
 * Returns digit count via *outDigits when non-NULL. */
static int ReadUint(const I18nDatetimeParser *p,
                     const char *text,
                     size_t len,
                     size_t at,
                     int minDigits,
                     int maxDigits,
                     int64_t *outV,
                     size_t *outNext,
                     int *overflow,
                     int *outDigits)
{
    int64_t v = 0;
    int count = 0;
    size_t cur = at;
    *overflow = 0;
    if (outDigits) {
        *outDigits = 0;
    }
    if (maxDigits < 1) {
        maxDigits = 1;
    }
    if (minDigits < 0) {
        minDigits = 0;
    }
    while (count < maxDigits) {
        int d = 0;
        size_t next = cur;
        if (!MatchDigit(p, text, len, cur, &d, &next)) {
            break;
        }
        if (v > (INT64_MAX - d) / 10) {
            *overflow = 1;
            return 0;
        }
        v = v * 10 + d;
        cur = next;
        count++;
    }
    if (count < minDigits) {
        return 0;
    }
    *outV = v;
    *outNext = cur;
    if (outDigits) {
        *outDigits = count;
    }
    return count > 0;
}

static I18nCalNameWidth WidthFromCount(uint8_t count)
{
    if (count >= 5) {
        return RoboI18nCalNameNarrow;
    }
    if (count >= 4) {
        return RoboI18nCalNameWide;
    }
    return RoboI18nCalNameAbbreviated;
}

static int MatchNameLongest(const char *text,
                              size_t len,
                              size_t at,
                              const char *const *names,
                              size_t nameCount,
                              size_t *outIdx,
                              size_t *outNext)
{
    size_t bestLen = 0;
    size_t bestI = 0;
    size_t i;
    int found = 0;
    for (i = 0; i < nameCount; i++) {
        size_t n;
        if (!names[i] || !names[i][0]) {
            continue;
        }
        n = strlen(names[i]);
        if (n == 0 || at + n > len) {
            continue;
        }
        if (memcmp(text + at, names[i], n) == 0 && n > bestLen) {
            bestLen = n;
            bestI = i;
            found = 1;
        }
    }
    if (!found) {
        return 0;
    }
    *outIdx = bestI;
    *outNext = at + bestLen;
    return 1;
}

static int MatchMonthName(const ParseState *st,
                            uint8_t count,
                            int permissive,
                            int32_t *outMonth,
                            size_t *outNext)
{
    const I18nDatetimeFormatter *fmt = &st->parser->fmt;
    I18nCalNameWidth widths[3];
    int nw = 0;
    int wi;
    widths[nw++] = WidthFromCount(count);
    if (permissive) {
        widths[nw++] = RoboI18nCalNameWide;
        widths[nw++] = RoboI18nCalNameAbbreviated;
    }
    for (wi = 0; wi < nw; wi++) {
        const char *names[12];
        uint8_t m;
        size_t idx = 0;
        size_t next = st->at;
        for (m = 1; m <= 12; m++) {
            const char *nm = NULL;
            if (I18nCalendarMonthName(&fmt->locale, &fmt->calendar, m, widths[wi], &nm) !=
                RoboOk) {
                names[m - 1] = "";
            } else {
                names[m - 1] = nm;
            }
        }
        if (MatchNameLongest(st->text, st->len, st->at, names, 12, &idx, &next)) {
            *outMonth = (int32_t)(idx + 1u);
            *outNext = next;
            return 1;
        }
    }
    return 0;
}

static int MatchWeekdayName(const ParseState *st,
                              uint8_t count,
                              int permissive,
                              int32_t *outIso,
                              size_t *outNext)
{
    const I18nDatetimeFormatter *fmt = &st->parser->fmt;
    I18nCalNameWidth widths[3];
    int nw = 0;
    int wi;
    /* ISO order Mon..Sun for match index → weekday */
    static const I18nWeekday kIso[7] = {
        RoboI18nWeekdayMon, RoboI18nWeekdayTue, RoboI18nWeekdayWed,
        RoboI18nWeekdayThu, RoboI18nWeekdayFri, RoboI18nWeekdaySat,
        RoboI18nWeekdaySun};
    widths[nw++] = WidthFromCount(count);
    if (permissive) {
        widths[nw++] = RoboI18nCalNameWide;
        widths[nw++] = RoboI18nCalNameAbbreviated;
    }
    for (wi = 0; wi < nw; wi++) {
        const char *names[7];
        int i;
        size_t idx = 0;
        size_t next = st->at;
        for (i = 0; i < 7; i++) {
            const char *nm = NULL;
            if (I18nCalendarWeekdayName(&fmt->locale, &fmt->calendar, kIso[i], widths[wi],
                                                &nm) != RoboOk) {
                names[i] = "";
            } else {
                names[i] = nm;
            }
        }
        if (MatchNameLongest(st->text, st->len, st->at, names, 7, &idx, &next)) {
            *outIso = (int32_t)kIso[idx];
            *outNext = next;
            return 1;
        }
    }
    return 0;
}

static int MatchEraName(const ParseState *st, int32_t *outEra, size_t *outNext)
{
    const I18nDatetimeFormatter *fmt = &st->parser->fmt;
    size_t bestLen = 0;
    int32_t bestEra = 0;
    int found = 0;
    size_t i;

    for (i = 0; i < CalEraNamesCount; i++) {
        const CalEraName *row = &CalEraNames[i];
        const char *cal = CalStr(row->calendar);
        const char *name;
        size_t n;
        if (!cal || strcmp(cal, fmt->calendar.id) != 0) {
            continue;
        }
        name = CalStr(row->name);
        if (!name || !name[0]) {
            continue;
        }
        n = strlen(name);
        if (st->at + n <= st->len && memcmp(st->text + st->at, name, n) == 0 && n > bestLen) {
            bestLen = n;
            bestEra = row->eraIndex;
            found = 1;
        }
    }
    /* Also try locale API for gregorian AD/BC indices 0/1. */
    if (!found) {
        int32_t ei;
        for (ei = 0; ei < 32; ei++) {
            const char *nm = NULL;
            size_t n;
            if (I18nCalendarEraName(&fmt->locale, &fmt->calendar, ei,
                                            RoboI18nCalNameAbbreviated, &nm) != RoboOk) {
                if (ei > 4) {
                    break;
                }
                continue;
            }
            n = strlen(nm);
            if (st->at + n <= st->len && memcmp(st->text + st->at, nm, n) == 0 && n > bestLen) {
                bestLen = n;
                bestEra = ei;
                found = 1;
            }
        }
    }
    if (!found) {
        return 0;
    }
    *outEra = bestEra;
    *outNext = st->at + bestLen;
    return 1;
}

static int MatchDayPeriod(const ParseState *st, uint8_t count, int *isPm, size_t *outNext)
{
    const I18nDatetimeFormatter *fmt = &st->parser->fmt;
    I18nCalNameWidth width = WidthFromCount(count);
    const char *am = NULL;
    const char *pm = NULL;
    size_t amLen = 0;
    size_t pmLen = 0;
    (void)I18nCalendarDayPeriodName(&fmt->locale, &fmt->calendar, "am", width, &am);
    (void)I18nCalendarDayPeriodName(&fmt->locale, &fmt->calendar, "pm", width, &pm);
    if (am) {
        amLen = strlen(am);
    }
    if (pm) {
        pmLen = strlen(pm);
    }
    /* Longest match wins when both could prefix. */
    if (pmLen > 0 && st->at + pmLen <= st->len &&
        memcmp(st->text + st->at, pm, pmLen) == 0 &&
        (amLen == 0 || pmLen >= amLen ||
         !(st->at + amLen <= st->len && memcmp(st->text + st->at, am, amLen) == 0))) {
        if (!(amLen > pmLen && st->at + amLen <= st->len &&
              memcmp(st->text + st->at, am, amLen) == 0)) {
            *isPm = 1;
            *outNext = st->at + pmLen;
            return 1;
        }
    }
    if (amLen > 0 && st->at + amLen <= st->len && memcmp(st->text + st->at, am, amLen) == 0) {
        if (pmLen > amLen && st->at + pmLen <= st->len &&
            memcmp(st->text + st->at, pm, pmLen) == 0) {
            *isPm = 1;
            *outNext = st->at + pmLen;
            return 1;
        }
        *isPm = 0;
        *outNext = st->at + amLen;
        return 1;
    }
    if (pmLen > 0 && st->at + pmLen <= st->len && memcmp(st->text + st->at, pm, pmLen) == 0) {
        *isPm = 1;
        *outNext = st->at + pmLen;
        return 1;
    }
    /* ASCII fallback */
    if (MatchAsciiCi(st->text, st->len, st->at, "PM", outNext) ||
        MatchAsciiCi(st->text, st->len, st->at, "P.M.", outNext)) {
        *isPm = 1;
        return 1;
    }
    if (MatchAsciiCi(st->text, st->len, st->at, "AM", outNext) ||
        MatchAsciiCi(st->text, st->len, st->at, "A.M.", outNext)) {
        *isPm = 0;
        return 1;
    }
    return 0;
}

static int AsciiDigit(char c)
{
    return c >= '0' && c <= '9' ? (c - '0') : -1;
}

static int ParseOffsetAscii(const char *text,
                              size_t len,
                              size_t at,
                              int allowZ,
                              int32_t *outOff,
                              size_t *outNext)
{
    size_t cur = at;
    int sign = 1;
    int hours = 0;
    int mins = 0;
    int secs = 0;
    int d;

    if (allowZ && cur < len && (text[cur] == 'Z' || text[cur] == 'z')) {
        *outOff = 0;
        *outNext = cur + 1u;
        return 1;
    }
    if (cur >= len) {
        return 0;
    }
    if (text[cur] == '+') {
        sign = 1;
        cur++;
    } else if (text[cur] == '-') {
        sign = -1;
        cur++;
    } else {
        return 0;
    }
    if (cur + 2u > len) {
        return 0;
    }
    d = AsciiDigit(text[cur]);
    if (d < 0) {
        return 0;
    }
    hours = d * 10;
    d = AsciiDigit(text[cur + 1u]);
    if (d < 0) {
        return 0;
    }
    hours += d;
    cur += 2u;
    if (cur < len && text[cur] == ':') {
        cur++;
        if (cur + 2u > len) {
            return 0;
        }
        d = AsciiDigit(text[cur]);
        if (d < 0) {
            return 0;
        }
        mins = d * 10;
        d = AsciiDigit(text[cur + 1u]);
        if (d < 0) {
            return 0;
        }
        mins += d;
        cur += 2u;
        if (cur < len && text[cur] == ':') {
            cur++;
            if (cur + 2u > len) {
                return 0;
            }
            d = AsciiDigit(text[cur]);
            if (d < 0) {
                return 0;
            }
            secs = d * 10;
            d = AsciiDigit(text[cur + 1u]);
            if (d < 0) {
                return 0;
            }
            secs += d;
            cur += 2u;
        }
    } else if (cur + 2u <= len && AsciiDigit(text[cur]) >= 0) {
        d = AsciiDigit(text[cur]);
        mins = d * 10;
        d = AsciiDigit(text[cur + 1u]);
        if (d < 0) {
            return 0;
        }
        mins += d;
        cur += 2u;
        if (cur + 2u <= len && AsciiDigit(text[cur]) >= 0) {
            d = AsciiDigit(text[cur]);
            secs = d * 10;
            d = AsciiDigit(text[cur + 1u]);
            if (d < 0) {
                return 0;
            }
            secs += d;
            cur += 2u;
        }
    }
    if (hours > 18 || mins > 59 || secs > 59) {
        return 0;
    }
    *outOff = sign * (hours * 3600 + mins * 60 + secs);
    *outNext = cur;
    return 1;
}

static int ParseGmtOrOffset(const ParseState *st, int32_t *outOff, size_t *outNext)
{
    size_t cur = st->at;
    size_t next = cur;

    if (MatchAsciiCi(st->text, st->len, cur, "GMT", &next) ||
        MatchAsciiCi(st->text, st->len, cur, "UTC", &next)) {
        cur = next;
        if (cur >= st->len || (st->text[cur] != '+' && st->text[cur] != '-')) {
            *outOff = 0;
            *outNext = cur;
            return 1;
        }
        return ParseOffsetAscii(st->text, st->len, cur, 0, outOff, outNext);
    }
    return ParseOffsetAscii(st->text, st->len, cur, 1, outOff, outNext);
}

static int ParseTzId(const ParseState *st, char *outId, size_t *outNext)
{
    size_t cur = st->at;
    size_t start = cur;
    size_t n;
    I18nTimezone tz;

    /* IANA-ish: Area/Location with ASCII letters, digits, _, -, / */
    while (cur < st->len) {
        unsigned char c = (unsigned char)st->text[cur];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '/' || c == '+') {
            cur++;
            continue;
        }
        break;
    }
    n = cur - start;
    if (n == 0 || n > RoboI18nTzIdMax) {
        return 0;
    }
    if (I18nTimezoneFromId(st->text + start, n, &tz) != RoboOk) {
        return 0;
    }
    memcpy(outId, tz.id, strlen(tz.id) + 1u);
    *outNext = cur;
    return 1;
}

static Result fail(I18nDtparseResult *out,
                          I18nDtparseErr err,
                          size_t off,
                          size_t consumed)
{
    out->error = err;
    out->errorOffset = off;
    out->consumed = consumed;
    return RoboParseError;
}

static int MatchLiteral(ParseState *st, const I18nDtfmtToken *tok, int permissive)
{
    const char *lit = st->parser->fmt.pattern + tok->litOff;
    size_t litLen = tok->litLen;
    size_t i;

    if (litLen == 0) {
        return 1;
    }
    if (permissive) {
        /* Allow Unicode whitespace where the literal is only whitespace. */
        int litWs = 1;
        size_t li = 0;
        while (li < litLen) {
            Uchar32 cp = 0;
            size_t n = li;
            if (UnicodeUtf8Next(lit, litLen, li, RoboUnicodeStrict, &cp, &n) !=
                RoboUnicodeOk) {
                litWs = 0;
                break;
            }
            if (!UnicodeIsWhitespace(cp)) {
                litWs = 0;
                break;
            }
            li = n;
        }
        if (litWs) {
            size_t next = SkipWs(st->text, st->len, st->at);
            if (next == st->at && litLen > 0) {
                /* Require at least some ws if literal had ws — permissive
                 * allows zero-width when optional separators appear. */
            }
            st->at = next;
            return 1;
        }
    }
    if (!PrefixEq(st->text, st->len, st->at, lit, litLen)) {
        return 0;
    }
    /* In permissive mode, also accept ASCII space for NBSP-like separators
     * of length 1 non-letter — handled above for pure-ws literals only. */
    (void)i;
    st->at += litLen;
    return 1;
}

static Result ParseField(ParseState *st,
                                 char field,
                                 uint8_t count,
                                 I18nDtparseResult *out)
{
    int permissive = st->parser->options.mode == RoboI18nParsePermissive;
    int overflow = 0;
    int64_t v = 0;
    size_t next = st->at;
    int minD;
    int maxD;

    switch (field) {
    case 'G':
        if (!MatchEraName(st, &st->eraIndex, &next)) {
            return fail(out, RoboI18nDtparseErrInvalidEra, st->at, st->at);
        }
        st->haveEra = 1;
        st->at = next;
        return RoboOk;

    case 'y':
    case 'Y':
    case 'u':
        if (count == 2) {
            minD = 2;
            maxD = permissive ? 4 : 2;
        } else {
            minD = permissive ? 1 : (count >= 4 ? 4 : count);
            if (minD < 1) {
                minD = 1;
            }
            maxD = count >= 4 ? 10 : (count > 4 ? count : 10);
            if (maxD < minD) {
                maxD = minD;
            }
        }
        {
            int digits = 0;
            if (!ReadUint(st->parser, st->text, st->len, st->at, minD, maxD, &v, &next,
                           &overflow, &digits)) {
                return fail(out,
                            overflow ? RoboI18nDtparseErrOverflow
                                     : RoboI18nDtparseErrInvalidField,
                            st->at, st->at);
            }
            if (count == 2 && digits == 2) {
                /* Century windows are calendar-aware and deterministic
                 * (no wall-clock pivot). */
                int yy = (int)v;
                if (st->parser->fmt.calendar.kind == RoboI18nCalBuddhist) {
                    /* BE years 2500–2599 for yy 00–99 (covers contemporary Thai). */
                    st->year = 2500 + yy;
                } else {
                    /* Gregorian-style: 1970–2069. */
                    st->year = (yy >= 70) ? (1900 + yy) : (2000 + yy);
                }
            } else {
                if (v < RoboI18nDateYearMin || v > RoboI18nDateYearMax) {
                    return fail(out, RoboI18nDtparseErrOverflow, st->at, st->at);
                }
                st->year = (int32_t)v;
            }
        }
        st->haveYear = 1;
        st->at = next;
        return RoboOk;

    case 'M':
    case 'L':
        if (count >= 3) {
            int32_t month = 0;
            if (!MatchMonthName(st, count, permissive, &month, &next)) {
                return fail(out, RoboI18nDtparseErrInvalidMonthName, st->at, st->at);
            }
            st->month = month;
            st->haveMonth = 1;
            st->at = next;
            return RoboOk;
        }
        minD = permissive ? 1 : count;
        maxD = 2;
        if (!ReadUint(st->parser, st->text, st->len, st->at, minD, maxD, &v, &next, &overflow,
                       NULL) ||
            v < 1 || v > 12) {
            return fail(out, RoboI18nDtparseErrInvalidField, st->at, st->at);
        }
        st->month = (int32_t)v;
        st->haveMonth = 1;
        st->at = next;
        return RoboOk;

    case 'd':
        minD = permissive ? 1 : (count >= 2 ? 2 : 1);
        maxD = 2;
        if (!ReadUint(st->parser, st->text, st->len, st->at, minD, maxD, &v, &next, &overflow,
                       NULL) ||
            v < 1 || v > 31) {
            return fail(out, RoboI18nDtparseErrInvalidField, st->at, st->at);
        }
        st->day = (int32_t)v;
        st->haveDay = 1;
        st->at = next;
        return RoboOk;

    case 'E':
    case 'e':
    case 'c':
        if ((field == 'e' || field == 'c') && count <= 2) {
            minD = permissive ? 1 : count;
            maxD = 1;
            if (!ReadUint(st->parser, st->text, st->len, st->at, minD, maxD, &v, &next,
                           &overflow, NULL) ||
                v < 1 || v > 7) {
                return fail(out, RoboI18nDtparseErrInvalidField, st->at, st->at);
            }
            st->weekday = (int32_t)v;
            st->haveWeekday = 1;
            st->at = next;
            return RoboOk;
        }
        {
            int32_t wd = 0;
            if (!MatchWeekdayName(st, count, permissive, &wd, &next)) {
                return fail(out, RoboI18nDtparseErrInvalidWeekdayName, st->at, st->at);
            }
            st->weekday = wd;
            st->haveWeekday = 1;
            st->at = next;
            return RoboOk;
        }

    case 'a':
    case 'b':
    case 'B': {
        int isPm = 0;
        if (!MatchDayPeriod(st, count, &isPm, &next)) {
            return fail(out, RoboI18nDtparseErrInvalidDayPeriod, st->at, st->at);
        }
        st->haveAmpm = 1;
        st->ampmIsPm = isPm;
        st->at = next;
        return RoboOk;
    }

    case 'h':
    case 'H':
    case 'K':
    case 'k':
        minD = permissive ? 1 : (count >= 2 ? 2 : 1);
        maxD = 2;
        if (!ReadUint(st->parser, st->text, st->len, st->at, minD, maxD, &v, &next, &overflow,
                       NULL)) {
            return fail(out, RoboI18nDtparseErrInvalidField, st->at, st->at);
        }
        if (field == 'h' && (v < 1 || v > 12)) {
            return fail(out, RoboI18nDtparseErrInvalidTime, st->at, st->at);
        }
        if (field == 'K' && (v < 0 || v > 11)) {
            return fail(out, RoboI18nDtparseErrInvalidTime, st->at, st->at);
        }
        if (field == 'H' && (v < 0 || v > 23)) {
            return fail(out, RoboI18nDtparseErrInvalidTime, st->at, st->at);
        }
        if (field == 'k' && (v < 1 || v > 24)) {
            return fail(out, RoboI18nDtparseErrInvalidTime, st->at, st->at);
        }
        st->hourStyle = (int)field;
        st->hourRaw = (int32_t)v;
        st->haveHour = 1;
        st->at = next;
        return RoboOk;

    case 'm':
        minD = permissive ? 1 : (count >= 2 ? 2 : 1);
        maxD = 2;
        if (!ReadUint(st->parser, st->text, st->len, st->at, minD, maxD, &v, &next, &overflow,
                       NULL) ||
            v < 0 || v > 59) {
            return fail(out, RoboI18nDtparseErrInvalidTime, st->at, st->at);
        }
        st->minute = (int32_t)v;
        st->haveMinute = 1;
        st->at = next;
        return RoboOk;

    case 's':
        minD = permissive ? 1 : (count >= 2 ? 2 : 1);
        maxD = 2;
        if (!ReadUint(st->parser, st->text, st->len, st->at, minD, maxD, &v, &next, &overflow,
                       NULL) ||
            v < 0 || v > 59) {
            return fail(out, RoboI18nDtparseErrInvalidTime, st->at, st->at);
        }
        st->second = (int32_t)v;
        st->haveSecond = 1;
        st->at = next;
        return RoboOk;

    case 'S': {
        int digits = 0;
        uint8_t want = count > 9 ? 9 : count;
        minD = permissive ? 1 : (int)want;
        maxD = (int)want;
        if (!ReadUint(st->parser, st->text, st->len, st->at, minD, maxD, &v, &next, &overflow,
                       &digits)) {
            return fail(out, RoboI18nDtparseErrInvalidTime, st->at, st->at);
        }
        {
            uint32_t nano = 0;
            int i;
            char tmp[16];
            int dn = digits;
            int64_t t = v;
            if (dn > 9) {
                dn = 9;
            }
            for (i = dn - 1; i >= 0; i--) {
                tmp[i] = (char)('0' + (t % 10));
                t /= 10;
            }
            for (i = 0; i < 9; i++) {
                int dig = (i < dn) ? (tmp[i] - '0') : 0;
                nano = nano * 10u + (uint32_t)dig;
            }
            st->nano = nano;
            st->haveNano = 1;
            st->at = next;
            return RoboOk;
        }
    }

    case 'z':
    case 'v':
    case 'O':
        if (ParseGmtOrOffset(st, &st->offsetSeconds, &next)) {
            st->haveOffset = 1;
            st->at = next;
            return RoboOk;
        }
        if (ParseTzId(st, st->tzId, &next)) {
            st->haveTzId = 1;
            st->at = next;
            return RoboOk;
        }
        /* Localized zone display names are not reverse-mapped unless they
         * coincide with an IANA id / GMT / offset — avoid silent guessing. */
        return fail(out, RoboI18nDtparseErrInvalidTimezone, st->at, st->at);

    case 'V':
        if (ParseTzId(st, st->tzId, &next)) {
            st->haveTzId = 1;
            st->at = next;
            return RoboOk;
        }
        if (ParseGmtOrOffset(st, &st->offsetSeconds, &next)) {
            st->haveOffset = 1;
            st->at = next;
            return RoboOk;
        }
        return fail(out, RoboI18nDtparseErrInvalidTimezone, st->at, st->at);

    case 'Z':
    case 'X':
    case 'x':
        if (!ParseOffsetAscii(st->text, st->len, st->at, field == 'X' || field == 'x',
                                &st->offsetSeconds, &next)) {
            return fail(out, RoboI18nDtparseErrInvalidTimezone, st->at, st->at);
        }
        st->haveOffset = 1;
        st->at = next;
        return RoboOk;

    default:
        /* Unsupported letter: match literal letter×count like formatter emits. */
        {
            char tmp[16];
            uint8_t i;
            uint8_t n = count > 15 ? 15 : count;
            for (i = 0; i < n; i++) {
                tmp[i] = field;
            }
            if (!PrefixEq(st->text, st->len, st->at, tmp, n)) {
                return fail(out, RoboI18nDtparseErrUnexpectedChar, st->at, st->at);
            }
            st->at += n;
            return RoboOk;
        }
    }
}

static uint8_t HourTo24(ParseState *st)
{
    int32_t h = st->hourRaw;
    if (!st->haveHour) {
        return 0;
    }
    if (st->hourStyle == 'H') {
        return (uint8_t)h;
    }
    if (st->hourStyle == 'k') {
        return (uint8_t)(h == 24 ? 0 : h);
    }
    if (st->hourStyle == 'K') {
        if (st->haveAmpm && st->ampmIsPm) {
            return (uint8_t)(h + 12);
        }
        return (uint8_t)h;
    }
    /* 'h' 1..12 */
    if (st->haveAmpm) {
        if (st->ampmIsPm) {
            return (uint8_t)(h == 12 ? 12 : h + 12);
        }
        return (uint8_t)(h == 12 ? 0 : h);
    }
    /* No day period: treat as 1..12 morning (0..11) for h without am/pm —
     * ambiguous; leave as hour%12 with 12→0 only if permissive. */
    if (st->parser->options.mode == RoboI18nParsePermissive) {
        return (uint8_t)(h == 12 ? 0 : h);
    }
    return (uint8_t)(h == 12 ? 0 : h);
}

static I18nTzDisambiguation MapDisambig(I18nDtparseDisambig d)
{
    switch (d) {
    case RoboI18nDtparseDisambigEarlier:
        return RoboI18nTzDisambigEarlier;
    case RoboI18nDtparseDisambigLater:
        return RoboI18nTzDisambigLater;
    case RoboI18nDtparseDisambigCompatible:
        return RoboI18nTzDisambigEarlier;
    case RoboI18nDtparseDisambigReject:
    default:
        return RoboI18nTzDisambigReject;
    }
}

static Result FinalizeParse(ParseState *st,
                                    I18nDtparseResult *out,
                                    int requireZone)
{
    const I18nDatetimeParser *parser = st->parser;
    int permissive = parser->options.mode == RoboI18nParsePermissive;
    Result r;
    uint8_t hour24 = 0;

    out->haveEra = st->haveEra ? 1u : 0u;
    out->haveDayPeriod = st->haveAmpm ? 1u : 0u;
    out->dayPeriodIsPm = st->ampmIsPm ? 1u : 0u;
    out->fields.year = st->haveYear ? st->year : 0;
    out->fields.month = st->haveMonth ? (uint8_t)st->month : 0;
    out->fields.day = st->haveDay ? (uint8_t)st->day : 0;
    out->fields.eraIndex = st->haveEra ? st->eraIndex : 0;

    if (st->haveYear && st->haveMonth && st->haveDay) {
        if (!I18nCalendarHasConversion(&parser->fmt.calendar)) {
            return fail(out, RoboI18nDtparseErrUnsupportedCalendar, st->at, st->at);
        }
        r = I18nCalendarToGregorian(&parser->fmt.calendar, &out->fields,
                                            &out->gregorianDate);
        if (r != RoboOk) {
            return fail(out, RoboI18nDtparseErrInvalidDate, st->at, st->at);
        }
        if (!I18nDateIsValid(&out->gregorianDate)) {
            return fail(out, RoboI18nDtparseErrInvalidDate, st->at, st->at);
        }
        out->haveDate = 1;
        out->local.date = out->gregorianDate;

        if (st->haveWeekday && !permissive) {
            I18nWeekday wd;
            if (I18nDateDayOfWeek(&out->gregorianDate, &wd) == RoboOk &&
                (int32_t)wd != st->weekday) {
                return fail(out, RoboI18nDtparseErrInvalidDate, st->at, st->at);
            }
        }
    }
    /* Partial date fields remain in out->fields; haveDate stays 0. */

    if (st->haveHour || st->haveMinute || st->haveSecond || st->haveNano || st->haveAmpm) {
        if (st->haveHour && st->hourStyle == 'h' && !st->haveAmpm &&
            parser->options.mode == RoboI18nParseStrict) {
            return fail(out, RoboI18nDtparseErrInvalidDayPeriod, st->at, st->at);
        }
        hour24 = HourTo24(st);
        r = I18nTimeInit(&out->time, hour24, (uint8_t)(st->haveMinute ? st->minute : 0),
                                (uint8_t)(st->haveSecond ? st->second : 0),
                                st->haveNano ? st->nano : 0u);
        if (r != RoboOk || !I18nTimeIsValid(&out->time)) {
            return fail(out, RoboI18nDtparseErrInvalidTime, st->at, st->at);
        }
        out->haveTime = 1;
        out->local.time = out->time;
    }

    if (out->haveDate && out->haveTime) {
        (void)I18nDatetimeInit(&out->local, &out->gregorianDate, &out->time);
    }

    if (st->haveOffset) {
        out->haveOffset = 1;
        out->offsetSeconds = st->offsetSeconds;
        out->zoneExplicit = 1;
        {
            I18nTimezone fixed;
            if (I18nTimezoneFixedSeconds(st->offsetSeconds, &fixed) == RoboOk) {
                size_t n = strlen(fixed.id);
                if (n > RoboI18nTzIdMax) {
                    n = RoboI18nTzIdMax;
                }
                memcpy(out->timezoneId, fixed.id, n);
                out->timezoneId[n] = '\0';
                out->haveTimezone = 1;
            }
        }
    }
    if (st->haveTzId) {
        out->haveTimezone = 1;
        out->zoneExplicit = 1;
        memcpy(out->timezoneId, st->tzId, strlen(st->tzId) + 1u);
    }

    if (requireZone && !out->zoneExplicit &&
        !(parser->options.resolveInstant && parser->hasResolveTz)) {
        return fail(out, RoboI18nDtparseErrMissingTimezone, st->at, st->at);
    }

    if (parser->options.resolveInstant && out->haveDate && out->haveTime) {
        I18nTimezone tz;
        I18nTzResolve resolve = RoboI18nTzUnique;
        I18nTzDisambiguation pol = MapDisambig(parser->options.disambiguation);

        if (out->zoneExplicit && out->haveTimezone) {
            r = I18nTimezoneFromId(out->timezoneId, strlen(out->timezoneId), &tz);
            if (r != RoboOk) {
                return fail(out, RoboI18nDtparseErrInvalidTimezone, st->at, st->at);
            }
        } else if (parser->hasResolveTz) {
            tz = parser->fmt.timezone;
        } else {
            return fail(out, RoboI18nDtparseErrMissingTimezone, st->at, st->at);
        }

        r = I18nTimezoneToUtcEx(&tz, &out->local, pol, &out->instant, &resolve);
        out->tzResolve = resolve;
        if (resolve == RoboI18nTzNonexistent) {
            return fail(out, RoboI18nDtparseErrNonexistentLocal, st->at, st->at);
        }
        if (resolve == RoboI18nTzAmbiguous &&
            parser->options.disambiguation == RoboI18nDtparseDisambigReject) {
            return fail(out, RoboI18nDtparseErrAmbiguousLocal, st->at, st->at);
        }
        if (r != RoboOk) {
            if (resolve == RoboI18nTzAmbiguous) {
                return fail(out, RoboI18nDtparseErrAmbiguousLocal, st->at, st->at);
            }
            return fail(out, RoboI18nDtparseErrInvalidTimezone, st->at, st->at);
        }
        out->haveInstant = 1;
    }

    out->error = RoboI18nDtparseErrNone;
    out->errorOffset = st->at;
    out->consumed = st->at;
    return RoboOk;
}

static Result ParseCore(const I18nDatetimeParser *parser,
                                const char *text,
                                size_t len,
                                int requireZone,
                                I18nDtparseResult *out)
{
    ParseState st;
    size_t i;
    size_t bad = 0;
    Result r;
    int permissive;

    if (!out) {
        return RoboInvalidArgument;
    }
    I18nDtparseResultClear(out);
    if (!parser || !parser->ready || !text) {
        return RoboInvalidArgument;
    }
    if (len > RoboI18nDtparseInputMax) {
        out->error = RoboI18nDtparseErrInputTooLong;
        return RoboInvalidArgument;
    }
    if (len == 0) {
        return fail(out, RoboI18nDtparseErrEmpty, 0, 0);
    }
    if (!Utf8ValidSpan(text, len, &bad)) {
        return fail(out, RoboI18nDtparseErrInvalidUtf8, bad, 0);
    }

    memset(&st, 0, sizeof(st));
    st.parser = parser;
    st.text = text;
    st.len = len;
    st.at = 0;
    permissive = parser->options.mode == RoboI18nParsePermissive;

    if (parser->options.allowLeadingWs) {
        st.at = SkipWs(text, len, st.at);
        if (st.at >= len) {
            return fail(out, RoboI18nDtparseErrEmpty, st.at, st.at);
        }
    }

    for (i = 0; i < parser->fmt.tokenCount; i++) {
        const I18nDtfmtToken *tok = &parser->fmt.tokens[i];
        if (tok->isLiteral) {
            if (!MatchLiteral(&st, tok, permissive)) {
                return fail(out, RoboI18nDtparseErrUnexpectedChar, st.at, st.at);
            }
        } else {
            r = ParseField(&st, (char)tok->field, tok->count, out);
            if (r != RoboOk) {
                return r;
            }
        }
    }

    if (parser->options.allowTrailingWs) {
        st.at = SkipWs(text, len, st.at);
    }
    if (parser->options.requireFull && st.at < len) {
        return fail(out, RoboI18nDtparseErrTrailing, st.at, st.at);
    }

    return FinalizeParse(&st, out, requireZone);
}

Result I18nParseDate(const I18nDatetimeParser *parser,
                                   const char *text,
                                   size_t len,
                                   I18nDtparseResult *out)
{
    return ParseCore(parser, text, len, 0, out);
}

Result I18nParseTime(const I18nDatetimeParser *parser,
                                   const char *text,
                                   size_t len,
                                   I18nDtparseResult *out)
{
    return ParseCore(parser, text, len, 0, out);
}

Result I18nParseLocalDatetime(const I18nDatetimeParser *parser,
                                             const char *text,
                                             size_t len,
                                             I18nDtparseResult *out)
{
    return ParseCore(parser, text, len, 0, out);
}

Result I18nParseZonedDatetime(const I18nDatetimeParser *parser,
                                             const char *text,
                                             size_t len,
                                             I18nDtparseResult *out)
{
    return ParseCore(parser, text, len, 1, out);
}
