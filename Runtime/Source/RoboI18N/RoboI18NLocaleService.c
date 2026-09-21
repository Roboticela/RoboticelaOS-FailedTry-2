/*
 * RoboI18N runtime locale service (Goal 4.16).
 */

#include "RoboI18N/RoboI18NLocaleService.h"

#include "RoboI18N/RoboCollData.h"
#include "RoboI18N/RoboDispData.h"
#include "RoboI18N/RoboHcData.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NLocaleNegotiate.h"
#include "RoboI18N/RoboI18NVersion.h"

#include <string.h>

static I18nLocale gApplicationLocale;
static int gApplicationLocaleSet;

void I18nLocaleOverridesInit(I18nLocaleOverrides *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

void I18nUnicodeKeywordsClear(I18nUnicodeKeywords *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}

void I18nLocaleSettingsClear(I18nLocaleSettings *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}

static void ToLowerCopy(const char *in, size_t n, char *out, size_t cap)
{
    size_t i;
    if (!out || cap == 0) {
        return;
    }
    if (n >= cap) {
        n = cap - 1u;
    }
    for (i = 0; i < n; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        out[i] = c;
    }
    out[n] = '\0';
}

static int KeyEq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void StoreKnown(I18nUnicodeKeywords *kw, const char *key, const char *type)
{
    if (KeyEq(key, "ca")) {
        strncpy(kw->calendar, type, sizeof(kw->calendar) - 1u);
    } else if (KeyEq(key, "nu")) {
        strncpy(kw->numberingSystem, type, sizeof(kw->numberingSystem) - 1u);
    } else if (KeyEq(key, "hc")) {
        strncpy(kw->hourCycle, type, sizeof(kw->hourCycle) - 1u);
    } else if (KeyEq(key, "co")) {
        strncpy(kw->collation, type, sizeof(kw->collation) - 1u);
    } else if (KeyEq(key, "tz")) {
        strncpy(kw->timezone, type, sizeof(kw->timezone) - 1u);
    } else {
        size_t n = strlen(kw->other);
        size_t kn = strlen(key);
        size_t tn = strlen(type);
        if (n + kn + tn + 3u < sizeof(kw->other)) {
            if (n > 0) {
                kw->other[n++] = '-';
            }
            memcpy(kw->other + n, key, kn);
            n += kn;
            kw->other[n++] = '-';
            memcpy(kw->other + n, type, tn);
            n += tn;
            kw->other[n] = '\0';
        }
    }
}

Result I18nLocaleUnicodeKeywords(const I18nLocale *loc,
                                                I18nUnicodeKeywords *out)
{
    const char *p;
    char key[RoboI18nUkeyMax + 1u];
    char type[RoboI18nUtypeMax + 1u];
    size_t n;

    if (!loc || !out) {
        return RoboInvalidArgument;
    }
    I18nUnicodeKeywordsClear(out);
    p = loc->extensions;
    if (!p[0]) {
        return RoboOk;
    }
    /* Expect leading "u-" */
    if (!(p[0] == 'u' || p[0] == 'U') || p[1] != '-') {
        return RoboOk; /* non-u extension: ignore for keywords */
    }
    p += 2;
    while (*p) {
        /* attribute (3–8) or key (2) */
        n = 0;
        while (p[n] && p[n] != '-' && n < RoboI18nUtypeMax) {
            n++;
        }
        if (n == 0) {
            return RoboParseError;
        }
        if (n == 2) {
            ToLowerCopy(p, 2, key, sizeof(key));
            p += 2;
            if (*p != '-') {
                return RoboParseError;
            }
            p++;
            n = 0;
            while (p[n] && p[n] != '-' && n < RoboI18nUtypeMax) {
                n++;
            }
            if (n < 3 || n > 8) {
                return RoboParseError;
            }
            ToLowerCopy(p, n, type, sizeof(type));
            p += n;
            StoreKnown(out, key, type);
            if (*p == '-') {
                p++;
            }
        } else {
            /* Unicode extension attribute — preserve in other */
            ToLowerCopy(p, n, type, sizeof(type));
            p += n;
            {
                size_t on = strlen(out->other);
                if (on + n + 2u < sizeof(out->other)) {
                    if (on) {
                        out->other[on++] = '-';
                    }
                    memcpy(out->other + on, type, n);
                    on += n;
                    out->other[on] = '\0';
                }
            }
            if (*p == '-') {
                p++;
            }
        }
    }
    return RoboOk;
}

static void AppendDash(char *buf, size_t cap, size_t *n)
{
    if (*n + 1u < cap) {
        buf[(*n)++] = '-';
        buf[*n] = '\0';
    }
}

static void AppendStr(char *buf, size_t cap, size_t *n, const char *s)
{
    size_t sn;
    if (!s || !s[0]) {
        return;
    }
    sn = strlen(s);
    if (*n + sn >= cap) {
        return;
    }
    memcpy(buf + *n, s, sn);
    *n += sn;
    buf[*n] = '\0';
}

Result I18nLocaleSetUnicodeKeywords(I18nLocale *loc,
                                                    const I18nUnicodeKeywords *kw)
{
    char buf[RoboI18nExtMax + 1u];
    size_t n = 0;
    if (!loc || !kw) {
        return RoboInvalidArgument;
    }
    buf[0] = '\0';
    AppendStr(buf, sizeof(buf), &n, "u");
    if (kw->other[0]) {
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, kw->other);
    }
    if (kw->calendar[0]) {
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, "ca");
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, kw->calendar);
    }
    if (kw->numberingSystem[0]) {
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, "nu");
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, kw->numberingSystem);
    }
    if (kw->hourCycle[0]) {
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, "hc");
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, kw->hourCycle);
    }
    if (kw->collation[0]) {
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, "co");
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, kw->collation);
    }
    if (kw->timezone[0]) {
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, "tz");
        AppendDash(buf, sizeof(buf), &n);
        AppendStr(buf, sizeof(buf), &n, kw->timezone);
    }
    if (n <= 1) {
        loc->extensions[0] = '\0';
        return RoboOk;
    }
    if (n >= sizeof(loc->extensions)) {
        return RoboError;
    }
    memcpy(loc->extensions, buf, n + 1u);
    return RoboOk;
}

Result I18nLocaleGetKeyword(const I18nLocale *loc,
                                           const char *key,
                                           char *out,
                                           size_t outCap)
{
    I18nUnicodeKeywords kw;
    const char *val = "";
    Result r;
    size_t n;
    if (!loc || !key || !out || outCap == 0) {
        return RoboInvalidArgument;
    }
    r = I18nLocaleUnicodeKeywords(loc, &kw);
    if (r != RoboOk) {
        return r;
    }
    if (KeyEq(key, "ca")) {
        val = kw.calendar;
    } else if (KeyEq(key, "nu")) {
        val = kw.numberingSystem;
    } else if (KeyEq(key, "hc")) {
        val = kw.hourCycle;
    } else if (KeyEq(key, "co")) {
        val = kw.collation;
    } else if (KeyEq(key, "tz")) {
        val = kw.timezone;
    } else {
        return RoboNotFound;
    }
    n = strlen(val);
    if (n + 1u > outCap) {
        return RoboError;
    }
    memcpy(out, val, n + 1u);
    return RoboOk;
}

Result I18nLocaleSetKeyword(I18nLocale *loc,
                                           const char *key,
                                           const char *type)
{
    I18nUnicodeKeywords kw;
    Result r;
    if (!loc || !key) {
        return RoboInvalidArgument;
    }
    r = I18nLocaleUnicodeKeywords(loc, &kw);
    if (r != RoboOk) {
        return r;
    }
    if (!type) {
        type = "";
    }
    if (KeyEq(key, "ca")) {
        strncpy(kw.calendar, type, sizeof(kw.calendar) - 1u);
        kw.calendar[sizeof(kw.calendar) - 1u] = '\0';
    } else if (KeyEq(key, "nu")) {
        strncpy(kw.numberingSystem, type, sizeof(kw.numberingSystem) - 1u);
        kw.numberingSystem[sizeof(kw.numberingSystem) - 1u] = '\0';
    } else if (KeyEq(key, "hc")) {
        strncpy(kw.hourCycle, type, sizeof(kw.hourCycle) - 1u);
        kw.hourCycle[sizeof(kw.hourCycle) - 1u] = '\0';
    } else if (KeyEq(key, "co")) {
        strncpy(kw.collation, type, sizeof(kw.collation) - 1u);
        kw.collation[sizeof(kw.collation) - 1u] = '\0';
    } else if (KeyEq(key, "tz")) {
        strncpy(kw.timezone, type, sizeof(kw.timezone) - 1u);
        kw.timezone[sizeof(kw.timezone) - 1u] = '\0';
    } else {
        return RoboNotSupported;
    }
    return I18nLocaleSetUnicodeKeywords(loc, &kw);
}

static const char *LookupHcForRegion(const char *region)
{
    uint32_t i;
    if (!region || !region[0]) {
        region = "001";
    }
    for (i = 0; i < HcRegionCount; i++) {
        if (strcmp(HcRegions[i].region, region) == 0) {
            return HcRegions[i].hourCycle;
        }
    }
    for (i = 0; i < HcRegionCount; i++) {
        if (strcmp(HcRegions[i].region, "001") == 0) {
            return HcRegions[i].hourCycle;
        }
    }
    return "h23";
}

I18nHourCycle I18nHourCycleFromId(const char *id)
{
    if (!id) {
        return RoboI18nHcUnknown;
    }
    if (strcmp(id, "h11") == 0) {
        return RoboI18nHcH11;
    }
    if (strcmp(id, "h12") == 0) {
        return RoboI18nHcH12;
    }
    if (strcmp(id, "h23") == 0) {
        return RoboI18nHcH23;
    }
    if (strcmp(id, "h24") == 0) {
        return RoboI18nHcH24;
    }
    return RoboI18nHcUnknown;
}

const char *I18nHourCycleId(I18nHourCycle hc)
{
    switch (hc) {
    case RoboI18nHcH11:
        return "h11";
    case RoboI18nHcH12:
        return "h12";
    case RoboI18nHcH23:
        return "h23";
    case RoboI18nHcH24:
        return "h24";
    default:
        return "";
    }
}

static void CopyField(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1u);
    dst[cap - 1u] = '\0';
}

Result I18nLocaleResolve(const I18nLocale *locale,
                                       const I18nLocaleOverrides *overrides,
                                       I18nLocaleSettings *out)
{
    I18nUnicodeKeywords kw;
    I18nLocale maximized;
    char region[8];
    Result r;

    if (!locale || !out || locale->language[0] == '\0') {
        return RoboInvalidArgument;
    }
    I18nLocaleSettingsClear(out);
    out->locale = *locale;
    r = I18nLocaleUnicodeKeywords(locale, &kw);
    if (r != RoboOk) {
        return r;
    }

    /* Calendar */
    if (overrides && overrides->calendar && overrides->calendar[0]) {
        CopyField(out->calendar, sizeof(out->calendar), overrides->calendar);
        out->calendarSrc = RoboI18nSrcOverride;
    } else if (kw.calendar[0]) {
        CopyField(out->calendar, sizeof(out->calendar), kw.calendar);
        out->calendarSrc = RoboI18nSrcExtension;
    } else if (I18nLocaleDefaultCalendar(locale, out->calendar, sizeof(out->calendar)) ==
               RoboOk) {
        out->calendarSrc = RoboI18nSrcLocaleDefault;
    } else {
        CopyField(out->calendar, sizeof(out->calendar), "gregorian");
        out->calendarSrc = RoboI18nSrcRuntimeFallback;
    }

    /* Numbering */
    if (overrides && overrides->numberingSystem && overrides->numberingSystem[0]) {
        CopyField(out->numberingSystem, sizeof(out->numberingSystem),
                   overrides->numberingSystem);
        out->numberingSrc = RoboI18nSrcOverride;
    } else if (kw.numberingSystem[0]) {
        CopyField(out->numberingSystem, sizeof(out->numberingSystem), kw.numberingSystem);
        out->numberingSrc = RoboI18nSrcExtension;
    } else if (I18nLocaleDefaultNumberingSystem(locale, out->numberingSystem,
                                                         sizeof(out->numberingSystem)) ==
               RoboOk) {
        out->numberingSrc = RoboI18nSrcLocaleDefault;
    } else {
        CopyField(out->numberingSystem, sizeof(out->numberingSystem), "latn");
        out->numberingSrc = RoboI18nSrcRuntimeFallback;
    }

    /* Hour cycle */
    if (overrides && overrides->hourCycle && overrides->hourCycle[0]) {
        CopyField(out->hourCycle, sizeof(out->hourCycle), overrides->hourCycle);
        out->hourCycleSrc = RoboI18nSrcOverride;
    } else if (kw.hourCycle[0]) {
        CopyField(out->hourCycle, sizeof(out->hourCycle), kw.hourCycle);
        out->hourCycleSrc = RoboI18nSrcExtension;
    } else {
        region[0] = '\0';
        if (I18nLocaleMaximize(locale, &maximized) == RoboOk && maximized.region[0]) {
            CopyField(region, sizeof(region), maximized.region);
        } else if (locale->region[0]) {
            CopyField(region, sizeof(region), locale->region);
        }
        CopyField(out->hourCycle, sizeof(out->hourCycle), LookupHcForRegion(region));
        out->hourCycleSrc = RoboI18nSrcLocaleDefault;
    }

    /* Collation type */
    if (overrides && overrides->collationType && overrides->collationType[0]) {
        CopyField(out->collationType, sizeof(out->collationType), overrides->collationType);
        out->collationSrc = RoboI18nSrcOverride;
    } else if (kw.collation[0]) {
        CopyField(out->collationType, sizeof(out->collationType), kw.collation);
        out->collationSrc = RoboI18nSrcExtension;
    } else {
        CopyField(out->collationType, sizeof(out->collationType), "standard");
        out->collationSrc = RoboI18nSrcRuntimeFallback;
    }

    /* Timezone — never inferred from language alone */
    if (overrides && overrides->timezoneId && overrides->timezoneId[0]) {
        CopyField(out->timezoneId, sizeof(out->timezoneId), overrides->timezoneId);
        out->timezoneSrc = RoboI18nSrcOverride;
    } else if (kw.timezone[0]) {
        CopyField(out->timezoneId, sizeof(out->timezoneId), kw.timezone);
        out->timezoneSrc = RoboI18nSrcExtension;
    } else {
        out->timezoneId[0] = '\0';
        out->timezoneSrc = RoboI18nSrcUnset;
    }

    return RoboOk;
}

Result I18nLocaleResolveNumbering(const I18nLocale *locale,
                                                 const char *overrideId,
                                                 char *outId,
                                                 size_t outCap)
{
    I18nLocaleOverrides ov;
    I18nLocaleSettings st;
    Result r;
    I18nLocaleOverridesInit(&ov);
    ov.numberingSystem = overrideId;
    r = I18nLocaleResolve(locale, &ov, &st);
    if (r != RoboOk) {
        return r;
    }
    if (strlen(st.numberingSystem) + 1u > outCap) {
        return RoboError;
    }
    memcpy(outId, st.numberingSystem, strlen(st.numberingSystem) + 1u);
    return RoboOk;
}

Result I18nLocaleResolveCalendar(const I18nLocale *locale,
                                                const char *overrideId,
                                                char *outId,
                                                size_t outCap)
{
    I18nLocaleOverrides ov;
    I18nLocaleSettings st;
    Result r;
    I18nLocaleOverridesInit(&ov);
    ov.calendar = overrideId;
    r = I18nLocaleResolve(locale, &ov, &st);
    if (r != RoboOk) {
        return r;
    }
    if (strlen(st.calendar) + 1u > outCap) {
        return RoboError;
    }
    memcpy(outId, st.calendar, strlen(st.calendar) + 1u);
    return RoboOk;
}

Result I18nLocaleResolveHourCycle(const I18nLocale *locale,
                                                  const char *overrideId,
                                                  char *outId,
                                                  size_t outCap)
{
    I18nLocaleOverrides ov;
    I18nLocaleSettings st;
    Result r;
    I18nLocaleOverridesInit(&ov);
    ov.hourCycle = overrideId;
    r = I18nLocaleResolve(locale, &ov, &st);
    if (r != RoboOk) {
        return r;
    }
    if (strlen(st.hourCycle) + 1u > outCap) {
        return RoboError;
    }
    memcpy(outId, st.hourCycle, strlen(st.hourCycle) + 1u);
    return RoboOk;
}

Result I18nLocaleResolveCollation(const I18nLocale *locale,
                                                 const char *overrideId,
                                                 char *outId,
                                                 size_t outCap)
{
    I18nLocaleOverrides ov;
    I18nLocaleSettings st;
    Result r;
    I18nLocaleOverridesInit(&ov);
    ov.collationType = overrideId;
    r = I18nLocaleResolve(locale, &ov, &st);
    if (r != RoboOk) {
        return r;
    }
    if (strlen(st.collationType) + 1u > outCap) {
        return RoboError;
    }
    memcpy(outId, st.collationType, strlen(st.collationType) + 1u);
    return RoboOk;
}

Result I18nLocaleResolveTimezone(const I18nLocale *locale,
                                                const char *overrideId,
                                                char *outId,
                                                size_t outCap)
{
    I18nLocaleOverrides ov;
    I18nLocaleSettings st;
    Result r;
    I18nLocaleOverridesInit(&ov);
    ov.timezoneId = overrideId;
    r = I18nLocaleResolve(locale, &ov, &st);
    if (r != RoboOk) {
        return r;
    }
    if (!st.timezoneId[0]) {
        if (outCap < 4u) {
            return RoboError;
        }
        memcpy(outId, "UTC", 4);
        return RoboOk;
    }
    if (strlen(st.timezoneId) + 1u > outCap) {
        return RoboError;
    }
    memcpy(outId, st.timezoneId, strlen(st.timezoneId) + 1u);
    return RoboOk;
}

Result I18nLocaleFallbackChain(const I18nLocale *loc,
                                              I18nLocale *out,
                                              size_t cap,
                                              size_t *outCount)
{
    I18nLocale cur;
    size_t n = 0;
    int guard = 0;
    if (!loc || !outCount) {
        return RoboInvalidArgument;
    }
    *outCount = 0;
    if (cap == 0 || !out) {
        return RoboInvalidArgument;
    }
    cur = *loc;
    for (;;) {
        if (n >= cap || n >= RoboI18nFallbackMax) {
            break;
        }
        out[n++] = cur;
        {
            I18nLocale parent;
            if (I18nLocaleDataParent(&cur, &parent) != RoboOk) {
                break;
            }
            if (I18nLocaleEqual(&cur, &parent)) {
                break;
            }
            cur = parent;
        }
        if (++guard > (int)RoboI18nFallbackMax) {
            break;
        }
    }
    *outCount = n;
    return RoboOk;
}

Result I18nLocaleNegotiate(const I18nLocaleList *requested,
                                         const I18nLocale *available,
                                         size_t availableCount,
                                         I18nLocale *outBest,
                                         I18nMatch *outMatch)
{
    I18nPreferenceList *prefs = NULL;
    I18nNegotiateResult res;
    Result r;

    if (!requested || !available || availableCount == 0 || !outBest) {
        return RoboInvalidArgument;
    }
    r = I18nPreferenceListFromLocaleList(requested, &prefs);
    if (r != RoboOk) {
        return r;
    }
    r = I18nPreferenceListNormalize(prefs);
    if (r != RoboOk) {
        I18nPreferenceListDestroy(prefs);
        return r;
    }
    r = I18nLocaleNegotiateEx(prefs, available, availableCount, &res);
    I18nPreferenceListDestroy(prefs);
    if (r != RoboOk) {
        return r;
    }
    *outBest = res.selected;
    if (outMatch) {
        *outMatch = I18nNegotiateKindToMatch(res.kind);
    }
    return RoboOk;
}

Result I18nSetApplicationLocale(const I18nLocale *loc)
{
    if (!loc || loc->language[0] == '\0') {
        return RoboInvalidArgument;
    }
    gApplicationLocale = *loc;
    gApplicationLocaleSet = 1;
    return RoboOk;
}

Result I18nGetApplicationLocale(I18nLocale *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gApplicationLocaleSet) {
        return RoboNotFound;
    }
    *out = gApplicationLocale;
    return RoboOk;
}

void I18nClearApplicationLocale(void)
{
    memset(&gApplicationLocale, 0, sizeof(gApplicationLocale));
    gApplicationLocaleSet = 0;
}

Result I18nGetEffectiveLocale(I18nLocale *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (gApplicationLocaleSet) {
        *out = gApplicationLocale;
        return RoboOk;
    }
    return I18nGetDefaultLocale(out);
}

int I18nLocaleSupportsCalendar(const I18nLocale *loc, const char *calendarId)
{
    I18nCalendar cal;
    (void)loc;
    if (!calendarId) {
        return 0;
    }
    return I18nCalendarFromId(calendarId, strlen(calendarId), &cal) == RoboOk;
}

int I18nLocaleSupportsNumbering(const I18nLocale *loc, const char *nsId)
{
    I18nNumberingSystem ns;
    (void)loc;
    if (!nsId) {
        return 0;
    }
    return I18nNumberingSystemGet(nsId, &ns) == RoboOk;
}

int I18nLocaleSupportsCollationType(const I18nLocale *loc, const char *type)
{
    const char *lang;
    uint32_t i;
    uint16_t t;
    if (!loc || !type || !type[0]) {
        return 0;
    }
    if (strcmp(type, "standard") == 0 || strcmp(type, "search") == 0) {
        return 1; /* UCA always available */
    }
    lang = loc->language[0] ? loc->language : "root";
    for (i = 0; i < CollLocaleCount; i++) {
        const CollLocale *row = &CollLocales[i];
        const char *id = CollStringPool + row->idOffset;
        if (strcmp(id, lang) != 0 && strcmp(id, "root") != 0) {
            continue;
        }
        for (t = 0; t < row->typeCount; t++) {
            const CollType *ct = &CollTypes[row->typeBegin + t];
            const char *n = CollStringPool + ct->nameOffset;
            if (strcmp(n, type) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

int I18nLocaleHasDisplayNames(const I18nLocale *loc)
{
    uint32_t i;
    if (!loc || !loc->language[0]) {
        return 0;
    }
    for (i = 0; i < DispLanguageCount; i++) {
        const char *disp = DispString(DispLanguages[i].displayLocale);
        if (disp && strcmp(disp, loc->language) == 0) {
            return 1;
        }
    }
    return strcmp(loc->language, "en") == 0;
}

int I18nLocaleHasTimezoneDisplay(const I18nLocale *loc)
{
    return I18nLocaleHasDisplayNames(loc) && DispTzPatternCount > 0u;
}

int I18nTimezoneIdIsKnown(const char *id, size_t len)
{
    I18nTimezone tz;
    if (!id || len == 0) {
        return 0;
    }
    return I18nTimezoneFromId(id, len, &tz) == RoboOk;
}

void GetI18nDataVersions(I18nDataVersions *out)
{
    if (!out) {
        return;
    }
    out->apiVersion = RoboI18nVersionString;
    out->generatorVersion = RoboI18nGeneratorVersionString;
    out->cldrVersion = RoboCldrVersionString;
    out->unicodeVersion = RoboCldrUnicodeVersionString;
    out->tzdbVersion = RoboTzdbVersionStringPin;
    out->ianaFileDate = RoboIanaRegistryFileDate;
    out->ucaVersion = RoboUcaVersionStringPin;
    out->iso4217Source = RoboIso4217SourceString;
}
