/*
 * Calendar abstraction + CLDR metadata lookup (Goal 4.5).
 */

#include "RoboI18N/RoboI18NCalendar.h"

#include "RoboI18N/RoboCalData.h"
#include "RoboI18N/RoboI18NLocaleData.h"

#include <string.h>

typedef struct calAlias {
    const char *id;
    I18nCalendarKind kind;
} CalAlias;

static const CalAlias CAL_ALIASES[] = {
    {"gregorian", RoboI18nCalGregorian},
    {"gregory", RoboI18nCalGregorian},
    {"iso8601", RoboI18nCalIso8601},
    {"buddhist", RoboI18nCalBuddhist},
    {"japanese", RoboI18nCalJapanese},
    {"islamic", RoboI18nCalIslamic},
    {"islamic-civil", RoboI18nCalIslamic},
    {"islamic-umalqura", RoboI18nCalIslamic},
    {"islamic-tbla", RoboI18nCalIslamic},
    {"islamic-rgsa", RoboI18nCalIslamic},
    {"hebrew", RoboI18nCalHebrew},
    {"persian", RoboI18nCalPersian},
    {"chinese", RoboI18nCalChinese},
};

static const char *KindCanonicalId(I18nCalendarKind kind)
{
    switch (kind) {
    case RoboI18nCalGregorian:
        return "gregorian";
    case RoboI18nCalIso8601:
        return "iso8601";
    case RoboI18nCalBuddhist:
        return "buddhist";
    case RoboI18nCalJapanese:
        return "japanese";
    case RoboI18nCalIslamic:
        return "islamic";
    case RoboI18nCalHebrew:
        return "hebrew";
    case RoboI18nCalPersian:
        return "persian";
    case RoboI18nCalChinese:
        return "chinese";
    default:
        return "";
    }
}

static int EqCi(const char *a, size_t alen, const char *b)
{
    size_t i;
    size_t blen = strlen(b);
    if (alen != blen) {
        return 0;
    }
    for (i = 0; i < alen; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

Result I18nCalendarFromId(const char *id,
                                         size_t len,
                                         I18nCalendar *out)
{
    size_t i;
    if (!id || !out || len == 0 || len >= RoboI18nCalendarIdMax) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < sizeof(CAL_ALIASES) / sizeof(CAL_ALIASES[0]); i++) {
        if (EqCi(id, len, CAL_ALIASES[i].id)) {
            return I18nCalendarFromKind(CAL_ALIASES[i].kind, out);
        }
    }
    memset(out, 0, sizeof(*out));
    out->kind = RoboI18nCalUnknown;
    memcpy(out->id, id, len);
    out->id[len] = '\0';
    return RoboOk;
}

Result I18nCalendarFromKind(I18nCalendarKind kind,
                                           I18nCalendar *out)
{
    const char *cid;
    if (!out) {
        return RoboInvalidArgument;
    }
    cid = KindCanonicalId(kind);
    if (!cid[0]) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    memcpy(out->id, cid, strlen(cid) + 1u);
    return RoboOk;
}

const char *I18nCalendarId(const I18nCalendar *cal)
{
    return cal ? cal->id : "";
}

I18nCalendarKind MakeI18nCalendarKind(const I18nCalendar *cal)
{
    return cal ? cal->kind : RoboI18nCalUnknown;
}

int I18nCalendarHasConversion(const I18nCalendar *cal)
{
    if (!cal) {
        return 0;
    }
    switch (cal->kind) {
    case RoboI18nCalGregorian:
    case RoboI18nCalIso8601:
    case RoboI18nCalBuddhist:
    case RoboI18nCalJapanese:
        return 1;
    default:
        return 0;
    }
}

static int DateCmpYmd(int32_t y1, uint8_t m1, uint8_t d1, int32_t y2, uint8_t m2, uint8_t d2)
{
    if (y1 != y2) {
        return y1 < y2 ? -1 : 1;
    }
    if (m1 != m2) {
        return m1 < m2 ? -1 : 1;
    }
    if (d1 != d2) {
        return d1 < d2 ? -1 : 1;
    }
    return 0;
}

static const CalEraStart *JapaneseEraForGregorian(const I18nDate *g)
{
    size_t i;
    const CalEraStart *best = NULL;
    for (i = 0; i < CalJapaneseEraStartCount; i++) {
        const CalEraStart *e = &CalJapaneseEraStart[i];
        if (DateCmpYmd(g->year, g->month, g->day, e->startYear, e->startMonth, e->startDay) >= 0) {
            best = e;
        } else {
            break; /* chronological table */
        }
    }
    return best;
}

Result I18nCalendarToGregorian(const I18nCalendar *cal,
                                              const I18nCalendarFields *fields,
                                              I18nDate *outGregorian)
{
    if (!cal || !fields || !outGregorian) {
        return RoboInvalidArgument;
    }
    switch (cal->kind) {
    case RoboI18nCalGregorian:
        return I18nDateInit(outGregorian, fields->year, fields->month, fields->day);
    case RoboI18nCalIso8601:
        /* Fields year/month/day are Gregorian; ISO week uses dedicated APIs. */
        return I18nDateInit(outGregorian, fields->year, fields->month, fields->day);
    case RoboI18nCalBuddhist:
        /* BE year = Gregorian year + 543. */
        return I18nDateInit(outGregorian, fields->year - 543, fields->month, fields->day);
    case RoboI18nCalJapanese: {
        const CalEraStart *era = CalEraStartFind(fields->eraIndex);
        int32_t gy;
        if (!era) {
            return RoboNotFound;
        }
        if (fields->year < 1) {
            return RoboInvalidArgument;
        }
        gy = era->startYear + fields->year - 1;
        return I18nDateInit(outGregorian, gy, fields->month, fields->day);
    }
    case RoboI18nCalIslamic:
    case RoboI18nCalHebrew:
    case RoboI18nCalPersian:
    case RoboI18nCalChinese:
        return RoboNotSupported;
    default:
        return RoboNotSupported;
    }
}

Result I18nCalendarFromGregorian(const I18nCalendar *cal,
                                                const I18nDate *gregorian,
                                                I18nCalendarFields *outFields)
{
    if (!cal || !gregorian || !outFields || !I18nDateIsValid(gregorian)) {
        return RoboInvalidArgument;
    }
    memset(outFields, 0, sizeof(*outFields));
    switch (cal->kind) {
    case RoboI18nCalGregorian:
    case RoboI18nCalIso8601:
        outFields->year = gregorian->year;
        outFields->month = gregorian->month;
        outFields->day = gregorian->day;
        outFields->eraIndex = 0;
        return RoboOk;
    case RoboI18nCalBuddhist:
        outFields->year = gregorian->year + 543;
        outFields->month = gregorian->month;
        outFields->day = gregorian->day;
        outFields->eraIndex = 0;
        return RoboOk;
    case RoboI18nCalJapanese: {
        const CalEraStart *era = JapaneseEraForGregorian(gregorian);
        if (!era) {
            return RoboNotFound;
        }
        outFields->eraIndex = era->eraIndex;
        outFields->year = gregorian->year - era->startYear + 1;
        outFields->month = gregorian->month;
        outFields->day = gregorian->day;
        return RoboOk;
    }
    case RoboI18nCalIslamic:
    case RoboI18nCalHebrew:
    case RoboI18nCalPersian:
    case RoboI18nCalChinese:
        return RoboNotSupported;
    default:
        return RoboNotSupported;
    }
}

static void RegionForLocale(const I18nLocale *loc, char *out, size_t cap)
{
    I18nLocale maxed;
    if (!loc || !out || cap < 4) {
        return;
    }
    out[0] = '\0';
    if (loc->region[0]) {
        size_t n = strlen(loc->region);
        if (n >= cap) {
            n = cap - 1u;
        }
        memcpy(out, loc->region, n);
        out[n] = '\0';
        return;
    }
    if (I18nLocaleMaximize(loc, &maxed) == RoboOk && maxed.region[0]) {
        size_t n = strlen(maxed.region);
        if (n >= cap) {
            n = cap - 1u;
        }
        memcpy(out, maxed.region, n);
        out[n] = '\0';
    }
}

Result I18nLocaleFirstDayOfWeek(const I18nLocale *loc,
                                                 I18nWeekday *out)
{
    char region[8];
    uint8_t fd;
    if (!loc || !out) {
        return RoboInvalidArgument;
    }
    RegionForLocale(loc, region, sizeof(region));
    fd = CalWeekFirstDay(region[0] ? region : "001");
    if (fd < 1 || fd > 7) {
        return RoboError;
    }
    *out = (I18nWeekday)fd;
    return RoboOk;
}

Result I18nLocaleWeekend(const I18nLocale *loc,
                                       I18nWeekday *outStart,
                                       I18nWeekday *outEnd)
{
    char region[8];
    uint8_t a, b;
    if (!loc || !outStart || !outEnd) {
        return RoboInvalidArgument;
    }
    RegionForLocale(loc, region, sizeof(region));
    a = CalWeekWeekendStart(region[0] ? region : "001");
    b = CalWeekWeekendEnd(region[0] ? region : "001");
    if (a < 1 || a > 7 || b < 1 || b > 7) {
        return RoboError;
    }
    *outStart = (I18nWeekday)a;
    *outEnd = (I18nWeekday)b;
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
    if (loc->script[0] && n + 5 < cap) {
        buf[n++] = '-';
        memcpy(buf + n, loc->script, 4);
        n += 4;
        buf[n] = '\0';
    }
    if (loc->region[0] && n + 1 + strlen(loc->region) < cap) {
        buf[n++] = '-';
        memcpy(buf + n, loc->region, strlen(loc->region));
        n += strlen(loc->region);
        buf[n] = '\0';
    }
}

static const CalSymbols *SymbolsResolve(const I18nLocale *loc,
                                                 const I18nCalendar *cal)
{
    I18nLocale cur;
    int guard = 0;
    char tag[RoboI18nTagMax];
    const char *cid;
    const CalSymbols *row;

    if (!loc || !cal) {
        return NULL;
    }
    cid = cal->id;
    if (cal->kind == RoboI18nCalIso8601) {
        cid = "gregorian"; /* ISO week calendar reuses Gregorian names/patterns */
    }
    cur = *loc;
    while (guard++ < 16) {
        FormatLocaleTag(&cur, tag, sizeof(tag));
        row = CalSymbolsFind(tag, cid);
        if (row) {
            return row;
        }
        if (cur.language[0] && strcmp(cur.language, "root") != 0) {
            char lang[RoboI18nLangMax + 1u];
            memcpy(lang, cur.language, sizeof(lang));
            row = CalSymbolsFind(lang, cid);
            if (row) {
                return row;
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
    return CalSymbolsFind("en", cid);
}

static size_t IsoToDayStorage(I18nWeekday wd)
{
    /* Mon=1..Sat=6,Sun=7 → storage sun..sat index: iso % 7 */
    return (size_t)((unsigned)wd % 7u);
}

Result I18nCalendarMonthName(const I18nLocale *loc,
                                            const I18nCalendar *cal,
                                            uint8_t month,
                                            I18nCalNameWidth width,
                                            const char **outUtf8)
{
    const CalSymbols *row;
    int32_t idx;
    if (!outUtf8 || month < 1 || month > 12) {
        return RoboInvalidArgument;
    }
    row = SymbolsResolve(loc, cal);
    if (!row) {
        return RoboNotFound;
    }
    if (width == RoboI18nCalNameWide) {
        idx = row->monthsWide[month - 1u];
    } else if (width == RoboI18nCalNameAbbreviated) {
        idx = row->monthsAbbr[month - 1u];
    } else if (width == RoboI18nCalNameNarrow) {
        idx = row->monthsNarrow[month - 1u];
    } else {
        return RoboInvalidArgument;
    }
    *outUtf8 = CalStr(idx);
    return RoboOk;
}

Result I18nCalendarWeekdayName(const I18nLocale *loc,
                                              const I18nCalendar *cal,
                                              I18nWeekday weekday,
                                              I18nCalNameWidth width,
                                              const char **outUtf8)
{
    const CalSymbols *row;
    size_t si;
    int32_t idx;
    if (!outUtf8 || weekday < 1 || weekday > 7) {
        return RoboInvalidArgument;
    }
    row = SymbolsResolve(loc, cal);
    if (!row) {
        return RoboNotFound;
    }
    si = IsoToDayStorage(weekday);
    if (width == RoboI18nCalNameWide) {
        idx = row->daysWide[si];
    } else if (width == RoboI18nCalNameNarrow) {
        idx = row->daysNarrow[si];
    } else {
        idx = row->daysAbbr[si];
    }
    *outUtf8 = CalStr(idx);
    return RoboOk;
}

Result I18nCalendarEraName(const I18nLocale *loc,
                                          const I18nCalendar *cal,
                                          int32_t eraIndex,
                                          I18nCalNameWidth width,
                                          const char **outUtf8)
{
    I18nLocale cur;
    int guard = 0;
    char tag[RoboI18nTagMax];
    const CalEraName *row;
    (void)width; /* only abbr table generated in Goal 4.5 */

    if (!loc || !cal || !outUtf8) {
        return RoboInvalidArgument;
    }
    cur = *loc;
    while (guard++ < 16) {
        FormatLocaleTag(&cur, tag, sizeof(tag));
        row = CalEraNameFind(tag, cal->id, eraIndex);
        if (!row) {
            row = CalEraNameFind(cur.language, cal->id, eraIndex);
        }
        if (row) {
            *outUtf8 = CalStr(row->name);
            return RoboOk;
        }
        {
            I18nLocale parent;
            if (I18nLocaleDataParent(&cur, &parent) != RoboOk) {
                break;
            }
            cur = parent;
        }
    }
    row = CalEraNameFind("en", cal->id, eraIndex);
    if (!row) {
        return RoboNotFound;
    }
    *outUtf8 = CalStr(row->name);
    return RoboOk;
}

Result I18nCalendarDayPeriodName(const I18nLocale *loc,
                                                 const I18nCalendar *cal,
                                                 const char *periodId,
                                                 I18nCalNameWidth width,
                                                 const char **outUtf8)
{
    const CalSymbols *row;
    (void)width;
    if (!periodId || !outUtf8) {
        return RoboInvalidArgument;
    }
    row = SymbolsResolve(loc, cal);
    if (!row) {
        return RoboNotFound;
    }
    if (strcmp(periodId, "am") == 0) {
        *outUtf8 = CalStr(row->am);
        return RoboOk;
    }
    if (strcmp(periodId, "pm") == 0) {
        *outUtf8 = CalStr(row->pm);
        return RoboOk;
    }
    return RoboNotFound;
}

static Result PatternFromStyle(const CalSymbols *row,
                                        I18nCalFormatStyle style,
                                        int which,
                                        const char **outUtf8)
{
    int32_t idx;
    if (!row || !outUtf8) {
        return RoboInvalidArgument;
    }
    if (which == 0) { /* date */
        switch (style) {
        case RoboI18nCalStyleFull:
            idx = row->dateFull;
            break;
        case RoboI18nCalStyleLong:
            idx = row->dateLong;
            break;
        case RoboI18nCalStyleMedium:
            idx = row->dateMedium;
            break;
        case RoboI18nCalStyleShort:
            idx = row->dateShort;
            break;
        default:
            return RoboInvalidArgument;
        }
    } else if (which == 1) { /* time */
        switch (style) {
        case RoboI18nCalStyleFull:
            idx = row->timeFull;
            break;
        case RoboI18nCalStyleLong:
            idx = row->timeLong;
            break;
        case RoboI18nCalStyleMedium:
            idx = row->timeMedium;
            break;
        case RoboI18nCalStyleShort:
            idx = row->timeShort;
            break;
        default:
            return RoboInvalidArgument;
        }
    } else {
        switch (style) {
        case RoboI18nCalStyleFull:
            idx = row->datetimeFull;
            break;
        case RoboI18nCalStyleLong:
            idx = row->datetimeLong;
            break;
        case RoboI18nCalStyleMedium:
            idx = row->datetimeMedium;
            break;
        case RoboI18nCalStyleShort:
            idx = row->datetimeShort;
            break;
        default:
            return RoboInvalidArgument;
        }
    }
    *outUtf8 = CalStr(idx);
    return RoboOk;
}

Result I18nCalendarDatePattern(const I18nLocale *loc,
                                              const I18nCalendar *cal,
                                              I18nCalFormatStyle style,
                                              const char **outUtf8)
{
    const CalSymbols *row = SymbolsResolve(loc, cal);
    if (!row) {
        return RoboNotFound;
    }
    return PatternFromStyle(row, style, 0, outUtf8);
}

Result I18nCalendarTimePattern(const I18nLocale *loc,
                                              const I18nCalendar *cal,
                                              I18nCalFormatStyle style,
                                              const char **outUtf8)
{
    const CalSymbols *row = SymbolsResolve(loc, cal);
    if (!row) {
        return RoboNotFound;
    }
    return PatternFromStyle(row, style, 1, outUtf8);
}

Result I18nCalendarDatetimePattern(const I18nLocale *loc,
                                                  const I18nCalendar *cal,
                                                  I18nCalFormatStyle style,
                                                  const char **outUtf8)
{
    const CalSymbols *row = SymbolsResolve(loc, cal);
    if (!row) {
        return RoboNotFound;
    }
    return PatternFromStyle(row, style, 2, outUtf8);
}
