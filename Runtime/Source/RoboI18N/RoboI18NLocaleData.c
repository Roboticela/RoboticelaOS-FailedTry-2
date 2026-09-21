/*
 * CLDR locale-data lookups + likely subtags (Goal 4.2).
 */

#include "RoboI18N/RoboI18NLocaleData.h"

#include "RoboI18N/RoboCldrData.h"

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

static void append(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(dst);
    size_t m = strlen(src);
    if (n + m + 1u > cap) {
        return;
    }
    memcpy(dst + n, src, m + 1u);
}

static Result FormatTagSimple(const I18nLocale *loc, char *buf, size_t cap)
{
    size_t n = 0;
    size_t ln;
    if (!loc || !buf || cap < 2u || loc->language[0] == '\0') {
        return RoboInvalidArgument;
    }
    ln = strlen(loc->language);
    if (ln + 1u > cap) {
        return RoboInvalidArgument;
    }
    memcpy(buf, loc->language, ln);
    n = ln;
    if (loc->script[0]) {
        if (n + 5u >= cap) {
            return RoboInvalidArgument;
        }
        buf[n++] = '-';
        memcpy(buf + n, loc->script, 4u);
        n += 4u;
    }
    if (loc->region[0]) {
        size_t rn = strlen(loc->region);
        if (n + 1u + rn >= cap) {
            return RoboInvalidArgument;
        }
        buf[n++] = '-';
        memcpy(buf + n, loc->region, rn);
        n += rn;
    }
    buf[n] = '\0';
    return RoboOk;
}

static int32_t FindLikely(const char *key)
{
    int32_t k = CldrFindStr(key);
    if (k < 0) {
        return -1;
    }
    return CldrKvLookup(CldrLikely, CldrLikelyCount, k);
}

Result I18nLocaleMaximize(const I18nLocale *in, I18nLocale *out)
{
    char lang[16];
    char script[8];
    char region[8];
    char key[64];
    int32_t hit = -1;
    I18nLocale tmp;
    const char *to;

    if (!in || !out || in->language[0] == '\0') {
        return RoboInvalidArgument;
    }

    CopyId(lang, sizeof(lang), in->language);
    CopyId(script, sizeof(script), in->script);
    CopyId(region, sizeof(region), in->region);

    if (script[0] && region[0]) {
        key[0] = '\0';
        append(key, sizeof(key), lang);
        append(key, sizeof(key), "-");
        append(key, sizeof(key), script);
        append(key, sizeof(key), "-");
        append(key, sizeof(key), region);
        hit = FindLikely(key);
    }
    if (hit < 0 && region[0]) {
        key[0] = '\0';
        append(key, sizeof(key), lang);
        append(key, sizeof(key), "-");
        append(key, sizeof(key), region);
        hit = FindLikely(key);
    }
    if (hit < 0 && script[0]) {
        key[0] = '\0';
        append(key, sizeof(key), lang);
        append(key, sizeof(key), "-");
        append(key, sizeof(key), script);
        hit = FindLikely(key);
    }
    if (hit < 0) {
        hit = FindLikely(lang);
    }
    if (hit < 0 && script[0] && region[0]) {
        key[0] = '\0';
        append(key, sizeof(key), "und-");
        append(key, sizeof(key), script);
        append(key, sizeof(key), "-");
        append(key, sizeof(key), region);
        hit = FindLikely(key);
    }
    if (hit < 0 && region[0]) {
        key[0] = '\0';
        append(key, sizeof(key), "und-");
        append(key, sizeof(key), region);
        hit = FindLikely(key);
    }
    if (hit < 0 && script[0]) {
        key[0] = '\0';
        append(key, sizeof(key), "und-");
        append(key, sizeof(key), script);
        hit = FindLikely(key);
    }
    if (hit < 0) {
        hit = FindLikely("und");
    }

    if (hit < 0) {
        *out = *in;
        return RoboOk;
    }

    to = CldrStr(hit);
    if (I18nLocaleParse(to, strlen(to), &tmp) != RoboOk) {
        return RoboParseError;
    }
    *out = *in;
    if (!out->script[0] && tmp.script[0]) {
        memcpy(out->script, tmp.script, sizeof(out->script));
    }
    if (!out->region[0] && tmp.region[0]) {
        memcpy(out->region, tmp.region, sizeof(out->region));
    }
    if (strcmp(out->language, "und") == 0 && tmp.language[0]) {
        memcpy(out->language, tmp.language, sizeof(out->language));
    }
    return RoboOk;
}

Result I18nLocaleMinimize(const I18nLocale *in, I18nLocale *out)
{
    I18nLocale maxed;
    I18nLocale trial;
    I18nLocale check;
    Result r;

    if (!in || !out) {
        return RoboInvalidArgument;
    }
    r = I18nLocaleMaximize(in, &maxed);
    if (r != RoboOk) {
        return r;
    }
    *out = maxed;

    if (out->region[0]) {
        trial = *out;
        trial.region[0] = '\0';
        if (I18nLocaleMaximize(&trial, &check) == RoboOk &&
            I18nLocaleEqual(&check, &maxed)) {
            *out = trial;
        }
    }
    if (out->script[0]) {
        trial = *out;
        trial.script[0] = '\0';
        if (I18nLocaleMaximize(&trial, &check) == RoboOk &&
            I18nLocaleEqual(&check, &maxed)) {
            *out = trial;
        }
    }
    return RoboOk;
}

Result I18nLocaleDataParent(const I18nLocale *in, I18nLocale *out)
{
    char tag[RoboI18nTagMax];
    int32_t key;
    int32_t parentI;
    Result r;

    if (!in || !out) {
        return RoboInvalidArgument;
    }
    if (strcmp(in->language, "root") == 0) {
        return RoboNotFound;
    }

    r = FormatTagSimple(in, tag, sizeof(tag));
    if (r != RoboOk) {
        return r;
    }
    key = CldrFindStr(tag);
    if (key >= 0) {
        parentI = CldrKvLookup(CldrParent, CldrParentCount, key);
        if (parentI >= 0) {
            const char *p = CldrStr(parentI);
            if (strcmp(p, "root") == 0) {
                I18nLocaleClear(out);
                memcpy(out->language, "root", 5);
                return RoboOk;
            }
            return I18nLocaleParse(p, strlen(p), out);
        }
    }

    {
        I18nLocale bare = *in;
        bare.extensions[0] = '\0';
        bare.privateuse[0] = '\0';
        bare.variantCount = 0;
        r = I18nLocaleParent(&bare, out);
        if (r == RoboOk) {
            return RoboOk;
        }
    }

    I18nLocaleClear(out);
    memcpy(out->language, "root", 5);
    return RoboOk;
}

static Result LookupLocNum(const I18nLocale *loc,
                                    int wantNative,
                                    char *outId,
                                    size_t outCap)
{
    I18nLocale cur;
    int guard = 0;

    if (!loc || !outId || outCap == 0) {
        return RoboInvalidArgument;
    }
    cur = *loc;
    cur.extensions[0] = '\0';
    cur.privateuse[0] = '\0';
    cur.variantCount = 0;

    while (guard++ < 16) {
        char tag[RoboI18nTagMax];
        int32_t key;
        const CldrLocNum *row;
        I18nLocale parent;

        if (strcmp(cur.language, "root") == 0) {
            CopyId(outId, outCap, "latn");
            return RoboOk;
        }
        if (FormatTagSimple(&cur, tag, sizeof(tag)) != RoboOk) {
            return RoboInvalidArgument;
        }
        key = CldrFindStr(tag);
        if (key >= 0) {
            row = CldrLocNumFind(key);
            if (row) {
                int32_t idx = wantNative ? row->nativeNs : row->defaultNs;
                if (wantNative && idx < 0) {
                    idx = row->defaultNs;
                }
                CopyId(outId, outCap, CldrStr(idx));
                return RoboOk;
            }
        }
        if (I18nLocaleDataParent(&cur, &parent) != RoboOk) {
            break;
        }
        cur = parent;
    }
    CopyId(outId, outCap, "latn");
    return RoboOk;
}

Result I18nLocaleDefaultNumberingSystem(const I18nLocale *loc,
                                                        char *outId,
                                                        size_t outCap)
{
    return LookupLocNum(loc, 0, outId, outCap);
}

Result I18nLocaleNativeNumberingSystem(const I18nLocale *loc,
                                                       char *outId,
                                                       size_t outCap)
{
    return LookupLocNum(loc, 1, outId, outCap);
}

Result I18nNumberingSystemGet(const char *id,
                                             I18nNumberingSystem *out)
{
    int32_t key;
    const CldrNsys *row;

    if (!id || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    key = CldrFindStr(id);
    if (key < 0) {
        return RoboNotFound;
    }
    row = CldrNsysFind(key);
    if (!row) {
        return RoboNotFound;
    }
    CopyId(out->id, sizeof(out->id), CldrStr(row->id));
    CopyId(out->type, sizeof(out->type), CldrStr(row->type));
    CopyId(out->digits, sizeof(out->digits), CldrStr(row->digits));
    return RoboOk;
}

static const CldrCalpref *CalprefForLocale(const I18nLocale *loc)
{
    char region[8];
    int32_t key;
    const CldrCalpref *row;

    if (loc->region[0]) {
        CopyId(region, sizeof(region), loc->region);
    } else {
        I18nLocale maxed;
        if (I18nLocaleMaximize(loc, &maxed) == RoboOk && maxed.region[0]) {
            CopyId(region, sizeof(region), maxed.region);
        } else {
            CopyId(region, sizeof(region), "001");
        }
    }
    key = CldrFindStr(region);
    if (key >= 0) {
        row = CldrCalprefFind(key);
        if (row) {
            return row;
        }
    }
    key = CldrFindStr("001");
    if (key < 0) {
        return NULL;
    }
    return CldrCalprefFind(key);
}

Result I18nLocaleCalendars(const I18nLocale *loc,
                                         char outIds[][32],
                                         size_t cap,
                                         size_t *outCount)
{
    const CldrCalpref *row;
    size_t i;
    size_t n;

    if (!loc || !outIds || !outCount) {
        return RoboInvalidArgument;
    }
    row = CalprefForLocale(loc);
    if (!row) {
        *outCount = 0;
        return RoboNotFound;
    }
    n = row->count;
    if (n > cap) {
        n = cap;
    }
    for (i = 0; i < n; i++) {
        CopyId(outIds[i], 32, CldrStr(row->calendars[i]));
    }
    *outCount = n;
    return RoboOk;
}

Result I18nLocaleDefaultCalendar(const I18nLocale *loc,
                                                char *outId,
                                                size_t outCap)
{
    char ids[1][32];
    size_t n = 0;
    Result r;

    if (!outId || outCap == 0) {
        return RoboInvalidArgument;
    }
    r = I18nLocaleCalendars(loc, ids, 1, &n);
    if (r != RoboOk || n == 0) {
        CopyId(outId, outCap, "gregorian");
        return RoboOk;
    }
    CopyId(outId, outCap, ids[0]);
    return RoboOk;
}

Result I18nLocaleMeasurementSystem(const I18nLocale *loc,
                                                  I18nMeasurement *out)
{
    char region[8];
    int32_t key;
    int32_t val;

    if (!loc || !out) {
        return RoboInvalidArgument;
    }
    if (loc->region[0]) {
        CopyId(region, sizeof(region), loc->region);
    } else {
        I18nLocale maxed;
        if (I18nLocaleMaximize(loc, &maxed) == RoboOk && maxed.region[0]) {
            CopyId(region, sizeof(region), maxed.region);
        } else {
            CopyId(region, sizeof(region), "001");
        }
    }
    key = CldrFindStr(region);
    val = -1;
    if (key >= 0) {
        val = CldrKvLookup(CldrMeasure, CldrMeasureCount, key);
    }
    if (val < 0) {
        key = CldrFindStr("001");
        val = CldrKvLookup(CldrMeasure, CldrMeasureCount, key);
    }
    if (val < 0) {
        *out = RoboI18nMeasureMetric;
        return RoboOk;
    }
    *out = (I18nMeasurement)val;
    return RoboOk;
}

const char *I18nCldrUnicodeVersionString(void)
{
    return CldrUnicodeVersionString();
}
