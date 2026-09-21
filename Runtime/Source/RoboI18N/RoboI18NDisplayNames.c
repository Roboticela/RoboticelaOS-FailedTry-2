/*
 * RoboI18N locale display names (Goal 4.10).
 * CLDR 46 tables in RoboDispData. Fallback: display locale → parents → en → code.
 */

#include "RoboI18N/RoboI18NDisplayNames.h"

#include "RoboI18N/RoboDispData.h"
#include "RoboI18N/RoboI18NLocaleData.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

void I18nDisplayOptionsInit(I18nDisplayOptions *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->fallback = RoboI18nDispFallbackCode;
    out->dialect = 0;
}

const char *I18nDisplayCldrVersionString(void)
{
    return DispCldrVersionString();
}

static const I18nDisplayOptions *OptsOrDefault(
    const I18nDisplayOptions *options,
    I18nDisplayOptions *scratch)
{
    if (options) {
        return options;
    }
    I18nDisplayOptionsInit(scratch);
    return scratch;
}

static int StrEqIdx(int32_t idx, const char *s, size_t len)
{
    const char *p = DispString(idx);
    size_t n;
    if (!p) {
        return 0;
    }
    n = strlen(p);
    return n == len && memcmp(p, s, len) == 0;
}

static int FindLocaleIdx(const char *tag, size_t len, int32_t *outIdx)
{
    uint32_t i;
    for (i = 0; i < DispStringCount; i++) {
        if (StrEqIdx((int32_t)i, tag, len)) {
            *outIdx = (int32_t)i;
            return 1;
        }
    }
    return 0;
}

static int LookupNameRow(const DispNameRow *rows,
                           uint32_t count,
                           int32_t locIdx,
                           const char *code,
                           size_t codeLen,
                           int32_t *outName)
{
    uint32_t lo = 0;
    uint32_t hi = count;
    /* Find lower bound for locIdx */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (rows[mid].displayLocale < locIdx) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    while (lo < count && rows[lo].displayLocale == locIdx) {
        if (StrEqIdx(rows[lo].code, code, codeLen)) {
            *outName = rows[lo].name;
            return 1;
        }
        lo++;
    }
    return 0;
}

static Result WriteOut(const char *s,
                               char *out,
                               size_t outCap,
                               size_t *outLen)
{
    size_t n;
    if (!s || !outLen) {
        return RoboInvalidArgument;
    }
    n = strlen(s);
    *outLen = n;
    if (!out) {
        return RoboOk;
    }
    if (outCap < n + 1u) {
        *outLen = n + 1u;
        return RoboError;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return RoboOk;
}

static void FormatTag(const I18nLocale *loc, char *buf, size_t cap)
{
    if (!loc || cap == 0) {
        return;
    }
    (void)I18nLocaleFormat(loc, buf, cap, NULL);
}

static int NextDisplayLocale(I18nLocale *cur)
{
    I18nLocale parent;
    if (I18nLocaleDataParent(cur, &parent) == RoboOk) {
        *cur = parent;
        return 1;
    }
    if (I18nLocaleParent(cur, &parent) == RoboOk) {
        *cur = parent;
        return 1;
    }
    return 0;
}

/**
 * Walk display locale chain then "en". Return name string or NULL.
 */
static const char *ResolveName(const DispNameRow *rows,
                                uint32_t count,
                                const I18nLocale *displayLocale,
                                const char *code,
                                size_t codeLen)
{
    I18nLocale cur;
    char tag[128];
    int32_t locIdx;
    int32_t nameIdx;
    int guard = 0;

    if (displayLocale) {
        cur = *displayLocale;
    } else {
        (void)I18nLocaleParseCanonical("en", 2, &cur);
    }

    for (;;) {
        size_t tlen = 0;
        FormatTag(&cur, tag, sizeof(tag));
        tlen = strlen(tag);
        if (tlen && FindLocaleIdx(tag, tlen, &locIdx) &&
            LookupNameRow(rows, count, locIdx, code, codeLen, &nameIdx)) {
            return DispString(nameIdx);
        }
        if (!NextDisplayLocale(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }

    /* Ultimate table fallback: en (CLDR root often empty for display names). */
    if (FindLocaleIdx("en", 2, &locIdx) &&
        LookupNameRow(rows, count, locIdx, code, codeLen, &nameIdx)) {
        return DispString(nameIdx);
    }
    return NULL;
}

static Result FinishName(const char *name,
                                 const char *codeFallback,
                                 size_t codeLen,
                                 const I18nDisplayOptions *opt,
                                 char *out,
                                 size_t outCap,
                                 size_t *outLen)
{
    char tmp[128];
    if (name) {
        return WriteOut(name, out, outCap, outLen);
    }
    if (opt->fallback == RoboI18nDispFallbackCode) {
        if (codeLen >= sizeof(tmp)) {
            return RoboInvalidArgument;
        }
        memcpy(tmp, codeFallback, codeLen);
        tmp[codeLen] = '\0';
        return WriteOut(tmp, out, outCap, outLen);
    }
    if (outLen) {
        *outLen = 0;
    }
    if (out && outCap > 0) {
        out[0] = '\0';
    }
    return RoboNotFound;
}

static void NormalizeLangKey(const char *id, size_t len, char *out, size_t cap, size_t *outLen)
{
    /* Accept bare language or BCP 47; use primary language subtag lowercased. */
    I18nLocale loc;
    size_t i;
    if (I18nLocaleParseCanonical(id, len, &loc) == RoboOk && loc.language[0]) {
        size_t n = strlen(loc.language);
        if (n >= cap) {
            n = cap - 1;
        }
        memcpy(out, loc.language, n);
        out[n] = '\0';
        *outLen = n;
        return;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    for (i = 0; i < len; i++) {
        char c = id[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        out[i] = c;
    }
    out[len] = '\0';
    *outLen = len;
}

Result I18nLanguageDisplayName(const char *languageId,
                                              size_t languageLen,
                                              const I18nLocale *displayLocale,
                                              const I18nDisplayOptions *options,
                                              char *out,
                                              size_t outCap,
                                              size_t *outLen)
{
    I18nDisplayOptions scratch;
    const I18nDisplayOptions *opt;
    char key[64];
    size_t keyLen = 0;
    const char *name;

    if (!languageId || !outLen) {
        return RoboInvalidArgument;
    }
    opt = OptsOrDefault(options, &scratch);
    NormalizeLangKey(languageId, languageLen, key, sizeof(key), &keyLen);
    name = ResolveName(DispLanguages, DispLanguageCount, displayLocale, key,
                        keyLen);
    return FinishName(name, key, keyLen, opt, out, outCap, outLen);
}

Result I18nScriptDisplayName(const char *scriptId,
                                            size_t scriptLen,
                                            const I18nLocale *displayLocale,
                                            const I18nDisplayOptions *options,
                                            char *out,
                                            size_t outCap,
                                            size_t *outLen)
{
    I18nDisplayOptions scratch;
    const I18nDisplayOptions *opt;
    char key[16];
    size_t i;
    const char *name;

    if (!scriptId || scriptLen == 0 || !outLen) {
        return RoboInvalidArgument;
    }
    if (scriptLen >= sizeof(key)) {
        return RoboInvalidArgument;
    }
    opt = OptsOrDefault(options, &scratch);
    /* ISO 15924: 4 letters, Title case in CLDR keys (Latn). */
    for (i = 0; i < scriptLen; i++) {
        char c = scriptId[i];
        if (i == 0) {
            if (c >= 'a' && c <= 'z') {
                c = (char)(c - 'a' + 'A');
            }
        } else {
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a');
            }
        }
        key[i] = c;
    }
    key[scriptLen] = '\0';
    name = ResolveName(DispScripts, DispScriptCount, displayLocale, key,
                        scriptLen);
    return FinishName(name, key, scriptLen, opt, out, outCap, outLen);
}

Result I18nRegionDisplayName(const char *regionId,
                                            size_t regionLen,
                                            const I18nLocale *displayLocale,
                                            const I18nDisplayOptions *options,
                                            char *out,
                                            size_t outCap,
                                            size_t *outLen)
{
    I18nDisplayOptions scratch;
    const I18nDisplayOptions *opt;
    char key[16];
    size_t i;
    const char *name;

    if (!regionId || regionLen == 0 || !outLen) {
        return RoboInvalidArgument;
    }
    if (regionLen >= sizeof(key)) {
        return RoboInvalidArgument;
    }
    opt = OptsOrDefault(options, &scratch);
    for (i = 0; i < regionLen; i++) {
        char c = regionId[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        key[i] = c;
    }
    key[regionLen] = '\0';
    name = ResolveName(DispTerritories, DispTerritoryCount, displayLocale, key,
                        regionLen);
    return FinishName(name, key, regionLen, opt, out, outCap, outLen);
}

static int LookupCurrency(int32_t locIdx,
                           const char *code,
                           size_t codeLen,
                           const DispCurrencyRow **outRow)
{
    uint32_t lo = 0;
    uint32_t hi = DispCurrencyCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (DispCurrencies[mid].displayLocale < locIdx) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    while (lo < DispCurrencyCount &&
           DispCurrencies[lo].displayLocale == locIdx) {
        if (StrEqIdx(DispCurrencies[lo].code, code, codeLen)) {
            *outRow = &DispCurrencies[lo];
            return 1;
        }
        lo++;
    }
    return 0;
}

static const DispCurrencyRow *ResolveCurrency(
    const I18nLocale *displayLocale,
    const char *code,
    size_t codeLen)
{
    I18nLocale cur;
    char tag[128];
    int32_t locIdx;
    const DispCurrencyRow *row = NULL;
    int guard = 0;

    if (displayLocale) {
        cur = *displayLocale;
    } else {
        (void)I18nLocaleParseCanonical("en", 2, &cur);
    }
    for (;;) {
        FormatTag(&cur, tag, sizeof(tag));
        if (tag[0] && FindLocaleIdx(tag, strlen(tag), &locIdx) &&
            LookupCurrency(locIdx, code, codeLen, &row)) {
            return row;
        }
        if (!NextDisplayLocale(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }
    if (FindLocaleIdx("en", 2, &locIdx) &&
        LookupCurrency(locIdx, code, codeLen, &row)) {
        return row;
    }
    return NULL;
}

Result I18nCurrencyDisplayName(const char *currencyCode,
                                              size_t currencyLen,
                                              I18nCurrencyNameStyle style,
                                              const I18nLocale *displayLocale,
                                              const I18nDisplayOptions *options,
                                              char *out,
                                              size_t outCap,
                                              size_t *outLen)
{
    I18nDisplayOptions scratch;
    const I18nDisplayOptions *opt;
    char key[8];
    size_t i;
    const DispCurrencyRow *row;
    const char *name = NULL;

    if (!currencyCode || currencyLen == 0 || !outLen) {
        return RoboInvalidArgument;
    }
    if (currencyLen >= sizeof(key)) {
        return RoboInvalidArgument;
    }
    opt = OptsOrDefault(options, &scratch);
    for (i = 0; i < currencyLen; i++) {
        char c = currencyCode[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        key[i] = c;
    }
    key[currencyLen] = '\0';
    row = ResolveCurrency(displayLocale, key, currencyLen);
    if (row) {
        if (style == RoboI18nCurrencySymbol) {
            name = DispString(row->symbol);
        } else if (style == RoboI18nCurrencyNarrowSymbol) {
            name = DispString(row->narrowSymbol);
        } else {
            name = DispString(row->name);
        }
    }
    return FinishName(name, key, currencyLen, opt, out, outCap, outLen);
}

Result I18nCalendarDisplayName(const I18nCalendar *calendar,
                                              const I18nLocale *displayLocale,
                                              const I18nDisplayOptions *options,
                                              char *out,
                                              size_t outCap,
                                              size_t *outLen)
{
    I18nDisplayOptions scratch;
    const I18nDisplayOptions *opt;
    const char *id;
    const char *name;

    if (!calendar || !outLen) {
        return RoboInvalidArgument;
    }
    opt = OptsOrDefault(options, &scratch);
    id = I18nCalendarId(calendar);
    if (!id) {
        return RoboInvalidArgument;
    }
    /* iso8601 displays as gregorian calendar name when no dedicated entry */
    name = ResolveName(DispCalendars, DispCalendarCount, displayLocale, id,
                        strlen(id));
    if (!name && strcmp(id, "iso8601") == 0) {
        name = ResolveName(DispCalendars, DispCalendarCount, displayLocale,
                            "gregorian", 9);
    }
    return FinishName(name, id, strlen(id), opt, out, outCap, outLen);
}

Result I18nNumberingSystemDisplayName(
    const char *numberingId,
    size_t numberingLen,
    const I18nLocale *displayLocale,
    const I18nDisplayOptions *options,
    char *out,
    size_t outCap,
    size_t *outLen)
{
    I18nDisplayOptions scratch;
    const I18nDisplayOptions *opt;
    char key[32];
    size_t i;
    const char *name;

    if (!numberingId || numberingLen == 0 || !outLen) {
        return RoboInvalidArgument;
    }
    if (numberingLen >= sizeof(key)) {
        return RoboInvalidArgument;
    }
    opt = OptsOrDefault(options, &scratch);
    for (i = 0; i < numberingLen; i++) {
        char c = numberingId[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        key[i] = c;
    }
    key[numberingLen] = '\0';
    name = ResolveName(DispNumbering, DispNumberingCount, displayLocale, key,
                        numberingLen);
    return FinishName(name, key, numberingLen, opt, out, outCap, outLen);
}

static int LookupMzForIanaAt(const char *iana,
                                 size_t len,
                                 int64_t unixSec,
                                 char *mzOut,
                                 size_t mzCap)
{
    uint32_t i;
    char canon[RoboI18nTzIdMax + 1u];
    size_t canonLen = 0;
    I18nTimezone tz;
    const char *lookup = iana;
    size_t lookupLen = len;

    /* Prefer canonical IANA id for CLDR metazone tables. */
    if (I18nTimezoneFromId(iana, len, &tz) == RoboOk &&
        tz.kind == RoboI18nTzIana &&
        I18nTimezoneCanonicalId(&tz, canon, sizeof(canon), &canonLen) == RoboOk &&
        canonLen > 0 && canonLen < sizeof(canon)) {
        lookup = canon;
        lookupLen = canonLen;
    }

    for (i = 0; i < DispIanaMzRangeCount; i++) {
        const char *id = DispString(DispIanaMzRanges[i].ianaId);
        if (!id || strlen(id) != lookupLen || memcmp(id, lookup, lookupLen) != 0) {
            continue;
        }
        if (unixSec >= DispIanaMzRanges[i].fromUnix &&
            unixSec < DispIanaMzRanges[i].toUnix) {
            const char *mz = DispString(DispIanaMzRanges[i].metazone);
            if (!mz) {
                return 0;
            }
            strncpy(mzOut, mz, mzCap - 1u);
            mzOut[mzCap - 1u] = '\0';
            return 1;
        }
    }
    return 0;
}

static const DispTzMetazoneRow *ResolveMetazoneRow(
    const I18nLocale *displayLocale,
    const char *mz,
    size_t mzLen)
{
    I18nLocale cur;
    char tag[128];
    int32_t locIdx;
    int guard = 0;

    if (displayLocale) {
        cur = *displayLocale;
    } else {
        (void)I18nLocaleParseCanonical("en", 2, &cur);
    }
    for (;;) {
        uint32_t i;
        FormatTag(&cur, tag, sizeof(tag));
        if (tag[0] && FindLocaleIdx(tag, strlen(tag), &locIdx)) {
            for (i = 0; i < DispTzMetazoneCount; i++) {
                if (DispTzMetazones[i].displayLocale == locIdx &&
                    StrEqIdx(DispTzMetazones[i].metazone, mz, mzLen)) {
                    return &DispTzMetazones[i];
                }
            }
        }
        if (!NextDisplayLocale(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }
    if (FindLocaleIdx("en", 2, &locIdx)) {
        uint32_t i;
        for (i = 0; i < DispTzMetazoneCount; i++) {
            if (DispTzMetazones[i].displayLocale == locIdx &&
                StrEqIdx(DispTzMetazones[i].metazone, mz, mzLen)) {
                return &DispTzMetazones[i];
            }
        }
    }
    return NULL;
}

static const DispTzZoneRow *ResolveZoneRow(const I18nLocale *displayLocale,
                                                       const char *iana,
                                                       size_t ianaLen)
{
    I18nLocale cur;
    char tag[128];
    int32_t locIdx;
    int guard = 0;

    if (displayLocale) {
        cur = *displayLocale;
    } else {
        (void)I18nLocaleParseCanonical("en", 2, &cur);
    }
    for (;;) {
        uint32_t i;
        FormatTag(&cur, tag, sizeof(tag));
        if (tag[0] && FindLocaleIdx(tag, strlen(tag), &locIdx)) {
            for (i = 0; i < DispTzZoneCount; i++) {
                if (DispTzZones[i].displayLocale == locIdx &&
                    StrEqIdx(DispTzZones[i].ianaId, iana, ianaLen)) {
                    return &DispTzZones[i];
                }
            }
        }
        if (!NextDisplayLocale(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }
    if (FindLocaleIdx("en", 2, &locIdx)) {
        uint32_t i;
        for (i = 0; i < DispTzZoneCount; i++) {
            if (DispTzZones[i].displayLocale == locIdx &&
                StrEqIdx(DispTzZones[i].ianaId, iana, ianaLen)) {
                return &DispTzZones[i];
            }
        }
    }
    return NULL;
}

static int DigitGlyphAt(const char *glyphs, int digit, const char **out, size_t *outLen)
{
    size_t i;
    size_t n;
    const char *p;
    if (!glyphs || digit < 0 || digit > 9) {
        return 0;
    }
    p = glyphs;
    for (i = 0; i < (size_t)digit; i++) {
        if ((unsigned char)p[0] < 0x80u) {
            if (!p[0]) {
                return 0;
            }
            p++;
        } else if ((p[0] & 0xE0) == 0xC0 && p[1]) {
            p += 2;
        } else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
            p += 3;
        } else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            p += 4;
        } else {
            return 0;
        }
    }
    if ((unsigned char)p[0] < 0x80u) {
        n = 1;
    } else if ((p[0] & 0xE0) == 0xC0) {
        n = 2;
    } else if ((p[0] & 0xF0) == 0xE0) {
        n = 3;
    } else if ((p[0] & 0xF8) == 0xF0) {
        n = 4;
    } else {
        return 0;
    }
    *out = p;
    *outLen = n;
    return 1;
}

static void ApplyNumberingDigits(char *buf, size_t cap, const I18nLocale *loc)
{
    I18nNumberingSystem ns;
    char nsId[16];
    char tmp[128];
    size_t oi = 0;
    const char *src;
    if (!buf || !loc || cap == 0) {
        return;
    }
    if (I18nLocaleDefaultNumberingSystem(loc, nsId, sizeof(nsId)) != RoboOk) {
        return;
    }
    if (I18nNumberingSystemGet(nsId, &ns) != RoboOk || !ns.digits[0]) {
        return;
    }
    for (src = buf; *src && oi + 1 < sizeof(tmp);) {
        if (*src >= '0' && *src <= '9') {
            const char *g = NULL;
            size_t glen = 0;
            if (!DigitGlyphAt(ns.digits, *src - '0', &g, &glen) || oi + glen >= sizeof(tmp)) {
                tmp[oi++] = *src++;
                continue;
            }
            memcpy(tmp + oi, g, glen);
            oi += glen;
            src++;
        } else {
            tmp[oi++] = *src++;
        }
    }
    tmp[oi] = '\0';
    strncpy(buf, tmp, cap - 1u);
    buf[cap - 1u] = '\0';
}

static void FormatGmt(int32_t offsetSeconds,
                       const char *gmtFmt,
                       const char *gmtZero,
                       const char *hourFmt,
                       char *out,
                       size_t cap)
{
    char hm[32];
    int neg = offsetSeconds < 0;
    int32_t absS = neg ? -offsetSeconds : offsetSeconds;
    int hours = (int)(absS / 3600);
    int mins = (int)((absS % 3600) / 60);
    int secs = (int)(absS % 60);
    const char *posPat = hourFmt;
    const char *negPat = hourFmt;
    const char *semi = hourFmt ? strchr(hourFmt, ';') : NULL;
    char patternBuf[48];
    const char *pat;
    char *dst;
    const char *src;
    size_t oi = 0;

    if (offsetSeconds == 0 && gmtZero && gmtZero[0]) {
        strncpy(out, gmtZero, cap - 1u);
        out[cap - 1u] = '\0';
        return;
    }
    if (!gmtFmt) {
        gmtFmt = "GMT{0}";
    }
    if (!hourFmt) {
        hourFmt = "+HH:mm;-HH:mm";
        semi = strchr(hourFmt, ';');
    }
    if (semi) {
        size_t n = (size_t)(semi - hourFmt);
        if (n >= sizeof(patternBuf)) {
            n = sizeof(patternBuf) - 1;
        }
        memcpy(patternBuf, hourFmt, n);
        patternBuf[n] = '\0';
        posPat = patternBuf;
        negPat = semi + 1;
    }
    pat = neg ? negPat : posPat;
    dst = hm;
    for (src = pat; *src && (size_t)(dst - hm) < sizeof(hm) - 1; src++) {
        if (src[0] == 'H' && src[1] == 'H') {
            dst += sprintf(dst, "%02d", hours);
            src++;
        } else if (src[0] == 'H') {
            dst += sprintf(dst, "%d", hours);
        } else if (src[0] == 'm' && src[1] == 'm') {
            dst += sprintf(dst, "%02d", mins);
            src++;
        } else if (src[0] == 's' && src[1] == 's') {
            dst += sprintf(dst, "%02d", secs);
            src++;
        } else {
            *dst++ = *src;
        }
    }
    *dst = '\0';

    for (src = gmtFmt; *src && oi + 1 < cap; src++) {
        if (src[0] == '{' && src[1] == '0' && src[2] == '}') {
            size_t n = strlen(hm);
            if (oi + n >= cap) {
                break;
            }
            memcpy(out + oi, hm, n);
            oi += n;
            src += 2;
        } else {
            out[oi++] = *src;
        }
    }
    out[oi] = '\0';
}

static const DispTzPatternRow *ResolveTzPatterns(
    const I18nLocale *displayLocale)
{
    I18nLocale cur;
    char tag[128];
    int32_t locIdx;
    int guard = 0;
    if (displayLocale) {
        cur = *displayLocale;
    } else {
        (void)I18nLocaleParseCanonical("en", 2, &cur);
    }
    for (;;) {
        uint32_t i;
        FormatTag(&cur, tag, sizeof(tag));
        if (tag[0] && FindLocaleIdx(tag, strlen(tag), &locIdx)) {
            for (i = 0; i < DispTzPatternCount; i++) {
                if (DispTzPatterns[i].displayLocale == locIdx) {
                    return &DispTzPatterns[i];
                }
            }
        }
        if (!NextDisplayLocale(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }
    return NULL;
}

static const char *PickIdx(int32_t a, int32_t b, int32_t c)
{
    if (a >= 0) {
        return DispString(a);
    }
    if (b >= 0) {
        return DispString(b);
    }
    if (c >= 0) {
        return DispString(c);
    }
    return NULL;
}

static const char *NameFromZoneRow(const DispTzZoneRow *z,
                                      I18nTimezoneNameStyle style,
                                      int isDst)
{
    if (!z) {
        return NULL;
    }
    switch (style) {
    case RoboI18nTzDispLongGeneric:
        return PickIdx(z->longGeneric, z->longStandard, -1);
    case RoboI18nTzDispLongStandard:
        return PickIdx(z->longStandard, z->longGeneric, -1);
    case RoboI18nTzDispLongDaylight:
        return PickIdx(z->longDaylight, z->longGeneric, z->longStandard);
    case RoboI18nTzDispShortGeneric:
        return PickIdx(z->shortGeneric, z->shortStandard, -1);
    case RoboI18nTzDispShortStandard:
        return PickIdx(z->shortStandard, z->shortGeneric, -1);
    case RoboI18nTzDispShortDaylight:
        return PickIdx(z->shortDaylight, z->shortGeneric, z->shortStandard);
    case RoboI18nTzDispSpecificLong:
        return isDst ? PickIdx(z->longDaylight, z->longStandard, z->longGeneric)
                      : PickIdx(z->longStandard, z->longGeneric, -1);
    case RoboI18nTzDispSpecificShort:
        return isDst ? PickIdx(z->shortDaylight, z->shortStandard, z->shortGeneric)
                      : PickIdx(z->shortStandard, z->shortGeneric, -1);
    default:
        return NULL;
    }
}

static const char *NameFromMzRow(const DispTzMetazoneRow *row,
                                    I18nTimezoneNameStyle style,
                                    int isDst)
{
    if (!row) {
        return NULL;
    }
    switch (style) {
    case RoboI18nTzDispLongGeneric:
        return PickIdx(row->longGeneric, row->longStandard, -1);
    case RoboI18nTzDispLongStandard:
        return PickIdx(row->longStandard, row->longGeneric, -1);
    case RoboI18nTzDispLongDaylight:
        return PickIdx(row->longDaylight, row->longGeneric, row->longStandard);
    case RoboI18nTzDispShortGeneric:
        return PickIdx(row->shortGeneric, row->shortStandard, -1);
    case RoboI18nTzDispShortStandard:
        return PickIdx(row->shortStandard, row->shortGeneric, -1);
    case RoboI18nTzDispShortDaylight:
        return PickIdx(row->shortDaylight, row->shortGeneric, row->shortStandard);
    case RoboI18nTzDispSpecificLong:
        return isDst ? PickIdx(row->longDaylight, row->longStandard, row->longGeneric)
                      : PickIdx(row->longStandard, row->longGeneric, -1);
    case RoboI18nTzDispSpecificShort:
        return isDst ? PickIdx(row->shortDaylight, row->shortStandard, row->shortGeneric)
                      : PickIdx(row->shortStandard, row->shortGeneric, -1);
    default:
        return NULL;
    }
}

static int64_t InstantUnixSec(const I18nInstant *instant)
{
    if (!instant) {
        return 0;
    }
    if (instant->unixMs >= 0) {
        return instant->unixMs / 1000;
    }
    return -(((-instant->unixMs) + 999) / 1000);
}

Result I18nTimezoneDisplayNameAt(const I18nTimezone *tz,
                                                 const I18nInstant *instant,
                                                 I18nTimezoneNameStyle style,
                                                 const I18nLocale *displayLocale,
                                                 const I18nDisplayOptions *options,
                                                 char *out,
                                                 size_t outCap,
                                                 size_t *outLen)
{
    I18nDisplayOptions scratch;
    const I18nDisplayOptions *opt;
    char mz[64];
    char canon[RoboI18nTzIdMax + 1u];
    size_t canonLen = 0;
    const char *name = NULL;
    const char *iana;
    const char *lookupId;
    size_t lookupLen;
    int isDst = 0;
    int64_t sec;

    if (!tz || !outLen) {
        return RoboInvalidArgument;
    }
    opt = OptsOrDefault(options, &scratch);
    iana = tz->id[0] ? tz->id : "UTC";
    lookupId = iana;
    lookupLen = strlen(iana);
    sec = InstantUnixSec(instant);

    if (tz->kind == RoboI18nTzIana) {
        if (I18nTimezoneCanonicalId(tz, canon, sizeof(canon), &canonLen) == RoboOk &&
            canonLen > 0) {
            lookupId = canon;
            lookupLen = canonLen;
        }
        if (instant) {
            (void)I18nTimezoneIsDstAt(tz, instant, &isDst);
        }
    }

    if (style == RoboI18nTzDispIdentifier) {
        return FinishName(NULL, iana, strlen(iana), opt, out, outCap, outLen);
    }

    if (style == RoboI18nTzDispGmt) {
        int32_t off = 0;
        const DispTzPatternRow *pat;
        const char *gmtFmt = "GMT{0}";
        const char *gmtZero = "GMT";
        const char *hourFmt = "+HH:mm;-HH:mm";
        char buf[96];
        Result r;

        if (tz->kind == RoboI18nTzSystem) {
            return RoboNotSupported;
        }
        if (tz->kind == RoboI18nTzUtc || tz->kind == RoboI18nTzFixed) {
            off = tz->offsetSeconds;
        } else {
            if (!instant) {
                return RoboInvalidArgument;
            }
            r = I18nTimezoneOffsetAt(tz, instant, &off);
            if (r != RoboOk) {
                return r;
            }
        }
        pat = ResolveTzPatterns(displayLocale);
        if (pat) {
            gmtFmt = DispString(pat->gmtFormat);
            gmtZero = DispString(pat->gmtZeroFormat);
            hourFmt = DispString(pat->hourFormat);
        }
        FormatGmt(off, gmtFmt, gmtZero, hourFmt, buf, sizeof(buf));
        if (displayLocale) {
            ApplyNumberingDigits(buf, sizeof(buf), displayLocale);
        }
        return WriteOut(buf, out, outCap, outLen);
    }

    if (style == RoboI18nTzDispExemplarCity) {
        name = ResolveName(DispTzExemplars, DispTzExemplarCount, displayLocale,
                            lookupId, lookupLen);
        if (!name) {
            name = ResolveName(DispTzExemplars, DispTzExemplarCount,
                                displayLocale, iana, strlen(iana));
        }
        if (!name) {
            /* Structural id presentation: last path segment, '_' → ' ' (not a translation). */
            const char *slash = strrchr(lookupId, '/');
            const char *city = slash ? slash + 1 : lookupId;
            char pretty[RoboI18nTzIdMax + 1u];
            size_t i;
            size_t clen = strlen(city);
            if (clen >= sizeof(pretty)) {
                clen = sizeof(pretty) - 1u;
            }
            for (i = 0; i < clen; i++) {
                pretty[i] = (city[i] == '_') ? ' ' : city[i];
            }
            pretty[clen] = '\0';
            return FinishName(NULL, pretty, clen, opt, out, outCap, outLen);
        }
        return FinishName(name, lookupId, lookupLen, opt, out, outCap, outLen);
    }

    /* Zone-specific CLDR names first, then metazone. */
    name = NameFromZoneRow(ResolveZoneRow(displayLocale, lookupId, lookupLen), style,
                              isDst);
    if (!name &&
        LookupMzForIanaAt(lookupId, lookupLen, sec, mz, sizeof(mz))) {
        name = NameFromMzRow(ResolveMetazoneRow(displayLocale, mz, strlen(mz)), style,
                                isDst);
    }

    /* Style-specific fallbacks before identifier. */
    if (!name) {
        if (style == RoboI18nTzDispSpecificShort ||
            style == RoboI18nTzDispShortGeneric ||
            style == RoboI18nTzDispShortStandard ||
            style == RoboI18nTzDispShortDaylight) {
            /* short → long generic → exemplar → GMT → id */
            name = NameFromZoneRow(ResolveZoneRow(displayLocale, lookupId, lookupLen),
                                      RoboI18nTzDispLongGeneric, isDst);
            if (!name && LookupMzForIanaAt(lookupId, lookupLen, sec, mz, sizeof(mz))) {
                name = NameFromMzRow(ResolveMetazoneRow(displayLocale, mz, strlen(mz)),
                                        RoboI18nTzDispLongGeneric, isDst);
            }
        }
    }
    if (!name &&
        (style == RoboI18nTzDispSpecificLong || style == RoboI18nTzDispLongGeneric ||
         style == RoboI18nTzDispSpecificShort || style == RoboI18nTzDispShortGeneric)) {
        name = ResolveName(DispTzExemplars, DispTzExemplarCount, displayLocale,
                            lookupId, lookupLen);
    }
    if (!name &&
        (style == RoboI18nTzDispSpecificLong || style == RoboI18nTzDispSpecificShort ||
         style == RoboI18nTzDispLongGeneric || style == RoboI18nTzDispShortGeneric)) {
        /* GMT offset as last localized representation before identifier. */
        if (tz->kind != RoboI18nTzSystem &&
            (instant || tz->kind == RoboI18nTzUtc || tz->kind == RoboI18nTzFixed)) {
            return I18nTimezoneDisplayNameAt(tz, instant, RoboI18nTzDispGmt,
                                                      displayLocale, options, out, outCap,
                                                      outLen);
        }
    }

    return FinishName(name, lookupId, lookupLen, opt, out, outCap, outLen);
}

Result I18nTimezoneDisplayName(const I18nTimezone *tz,
                                              I18nTimezoneNameStyle style,
                                              const I18nLocale *displayLocale,
                                              const I18nDisplayOptions *options,
                                              char *out,
                                              size_t outCap,
                                              size_t *outLen)
{
    return I18nTimezoneDisplayNameAt(tz, NULL, style, displayLocale, options, out,
                                              outCap, outLen);
}

static void ApplyPattern2(const char *pat, const char *a, const char *b, char *out, size_t cap)
{
    size_t oi = 0;
    const char *src;
    for (src = pat; *src && oi + 1 < cap; src++) {
        if (src[0] == '{' && src[2] == '}') {
            const char *rep = (src[1] == '0') ? a : (src[1] == '1') ? b : NULL;
            if (rep) {
                size_t n = strlen(rep);
                if (oi + n >= cap) {
                    break;
                }
                memcpy(out + oi, rep, n);
                oi += n;
                src += 2;
                continue;
            }
        }
        out[oi++] = *src;
    }
    out[oi] = '\0';
}

static void GetPatterns(const I18nLocale *displayLocale,
                         const char **localePat,
                         const char **sepPat)
{
    I18nLocale cur;
    char tag[128];
    int32_t locIdx;
    int guard = 0;

    *localePat = "{0} ({1})";
    *sepPat = "{0}, {1}";
    if (displayLocale) {
        cur = *displayLocale;
    } else {
        (void)I18nLocaleParseCanonical("en", 2, &cur);
    }
    for (;;) {
        uint32_t i;
        FormatTag(&cur, tag, sizeof(tag));
        if (tag[0] && FindLocaleIdx(tag, strlen(tag), &locIdx)) {
            for (i = 0; i < DispPatternCount; i++) {
                if (DispPatterns[i].displayLocale == locIdx) {
                    *localePat = DispString(DispPatterns[i].localePattern);
                    *sepPat = DispString(DispPatterns[i].localeSeparator);
                    return;
                }
            }
        }
        if (!NextDisplayLocale(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }
}

Result I18nLocaleDisplayName(const I18nLocale *described,
                                            const I18nLocale *displayLocale,
                                            const I18nDisplayOptions *options,
                                            char *out,
                                            size_t outCap,
                                            size_t *outLen)
{
    I18nDisplayOptions scratch;
    const I18nDisplayOptions *opt;
    char lang[128];
    char script[128];
    char region[128];
    char rest[256];
    char composed[384];
    size_t n = 0;
    const char *localePat;
    const char *sepPat;
    int haveRest = 0;

    if (!described || !outLen) {
        return RoboInvalidArgument;
    }
    opt = OptsOrDefault(options, &scratch);

    if (I18nLanguageDisplayName(described->language, strlen(described->language),
                                        displayLocale, opt, lang, sizeof(lang), &n) !=
        RoboOk) {
        return RoboError;
    }

    rest[0] = '\0';
    if (described->script[0]) {
        if (I18nScriptDisplayName(described->script, strlen(described->script),
                                          displayLocale, opt, script, sizeof(script),
                                          &n) != RoboOk) {
            return RoboError;
        }
        strncpy(rest, script, sizeof(rest) - 1);
        haveRest = 1;
    }
    if (described->region[0]) {
        if (I18nRegionDisplayName(described->region, strlen(described->region),
                                          displayLocale, opt, region, sizeof(region),
                                          &n) != RoboOk) {
            return RoboError;
        }
        if (haveRest) {
            char tmp[256];
            GetPatterns(displayLocale, &localePat, &sepPat);
            ApplyPattern2(sepPat, rest, region, tmp, sizeof(tmp));
            strncpy(rest, tmp, sizeof(rest) - 1);
        } else {
            strncpy(rest, region, sizeof(rest) - 1);
            haveRest = 1;
        }
    }

    if (!haveRest) {
        return WriteOut(lang, out, outCap, outLen);
    }
    GetPatterns(displayLocale, &localePat, &sepPat);
    ApplyPattern2(localePat, lang, rest, composed, sizeof(composed));
    return WriteOut(composed, out, outCap, outLen);
}
