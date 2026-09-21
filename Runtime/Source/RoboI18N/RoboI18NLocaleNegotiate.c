/*
 * RoboI18N locale negotiation & Accept-Language (Goal 4.17).
 */

#include "RoboI18N/RoboI18NLocaleNegotiate.h"

#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct I18nPreferenceList {
    I18nLanguageRange *items;
    size_t count;
    size_t capacity;
};

void I18nLanguageRangeClear(I18nLanguageRange *r)
{
    if (r) {
        memset(r, 0, sizeof(*r));
        r->qMilli = RoboI18nQMilliDefault;
    }
}

void I18nNegotiateResultClear(I18nNegotiateResult *r)
{
    if (r) {
        memset(r, 0, sizeof(*r));
    }
}

I18nMatch I18nNegotiateKindToMatch(I18nNegotiateKind k)
{
    switch (k) {
    case RoboI18nNegExact:
        return RoboI18nMatchExact;
    case RoboI18nNegRegion:
        return RoboI18nMatchRegion;
    case RoboI18nNegScript:
        return RoboI18nMatchScript;
    case RoboI18nNegLanguage:
    case RoboI18nNegWildcard:
        return RoboI18nMatchLanguage;
    default:
        return RoboI18nMatchNone;
    }
}

static int IsAsciiWs(unsigned char c)
{
    return c == ' ' || c == '\t';
}

static int IsDigit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static int Utf8OkAsciiish(const char *s, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0) {
            return 0;
        }
        /* Allow printable ASCII + HTAB; reject other controls and high bytes
         * (language tags are ASCII). */
        if (c == '\t' || (c >= 0x20 && c <= 0x7E)) {
            continue;
        }
        return 0;
    }
    return 1;
}

/**
 * Parse q= value into millipercent. Accepts 0 / 1 / 0.xxx (≤3 digits).
 * Returns 0 on success; -1 on malformed / out of range.
 */
static int ParseQMilli(const char *s, size_t len, I18nQMilli *out)
{
    size_t i = 0;
    while (i < len && IsAsciiWs((unsigned char)s[i])) {
        i++;
    }
    while (len > i && IsAsciiWs((unsigned char)s[len - 1])) {
        len--;
    }
    if (i >= len) {
        return -1;
    }
    if (s[i] == '1') {
        i++;
        if (i < len && s[i] == '.') {
            i++;
            while (i < len && s[i] == '0') {
                i++;
            }
        }
        while (i < len && IsAsciiWs((unsigned char)s[i])) {
            i++;
        }
        if (i != len) {
            return -1;
        }
        *out = RoboI18nQMilliMax;
        return 0;
    }
    if (s[i] == '0') {
        unsigned v = 0;
        unsigned digits = 0;
        i++;
        if (i < len && s[i] == '.') {
            i++;
            while (i < len && IsDigit((unsigned char)s[i])) {
                if (digits >= 3u) {
                    return -1; /* >3 fractional digits */
                }
                v = v * 10u + (unsigned)(s[i] - '0');
                digits++;
                i++;
            }
            while (digits < 3u) {
                v *= 10u;
                digits++;
            }
        }
        while (i < len && IsAsciiWs((unsigned char)s[i])) {
            i++;
        }
        if (i != len) {
            return -1;
        }
        if (v > RoboI18nQMilliMax) {
            return -1;
        }
        *out = (I18nQMilli)v;
        return 0;
    }
    return -1;
}

Result I18nLanguageRangeParse(const char *text,
                                             size_t len,
                                             I18nLanguageRange *out)
{
    size_t tagEnd;
    size_t p;
    I18nQMilli q = RoboI18nQMilliDefault;
    int qExplicit = 0;
    size_t i;

    if (!text || !out || len == 0 || len > RoboI18nRangeMaxBytes) {
        return RoboInvalidArgument;
    }
    if (!Utf8OkAsciiish(text, len)) {
        return RoboParseError;
    }

    /* trim */
    i = 0;
    while (i < len && IsAsciiWs((unsigned char)text[i])) {
        i++;
    }
    while (len > i && IsAsciiWs((unsigned char)text[len - 1])) {
        len--;
    }
    if (i >= len) {
        return RoboParseError;
    }
    text += i;
    len -= i;

    tagEnd = 0;
    while (tagEnd < len && text[tagEnd] != ';') {
        tagEnd++;
    }
    {
        size_t te = tagEnd;
        while (te > 0 && IsAsciiWs((unsigned char)text[te - 1])) {
            te--;
        }
        tagEnd = te;
    }
    if (tagEnd == 0) {
        return RoboParseError;
    }

    if (tagEnd < len && text[tagEnd] == ';') {
        p = tagEnd + 1;
        while (p < len && IsAsciiWs((unsigned char)text[p])) {
            p++;
        }
        if (p + 2 > len || (text[p] != 'q' && text[p] != 'Q') || text[p + 1] != '=') {
            return RoboParseError;
        }
        if (ParseQMilli(text + p + 2, len - (p + 2), &q) != 0) {
            return RoboParseError;
        }
        qExplicit = 1;
    } else if (tagEnd != len) {
        return RoboParseError;
    }

    I18nLanguageRangeClear(out);
    out->qMilli = q;
    out->qExplicit = (uint8_t)qExplicit;

    if (tagEnd == 1 && text[0] == '*') {
        out->isWildcard = 1;
        return RoboOk;
    }

    /* Reject extended range forms like *-CH or en-* */
    {
        size_t j;
        for (j = 0; j < tagEnd; j++) {
            if (text[j] == '*') {
                return RoboParseError;
            }
        }
    }

    if (I18nLocaleParseCanonical(text, tagEnd, &out->locale) != RoboOk) {
        return RoboParseError;
    }
    if (out->locale.language[0] == '\0') {
        return RoboParseError;
    }
    return RoboOk;
}

static int ScriptsConflict(const I18nLocale *a, const I18nLocale *b)
{
    I18nLocale ma;
    I18nLocale mb;
    const char *sa;
    const char *sb;

    if (I18nLocaleMaximize(a, &ma) != RoboOk) {
        ma = *a;
    }
    if (I18nLocaleMaximize(b, &mb) != RoboOk) {
        mb = *b;
    }
    sa = ma.script[0] ? ma.script : a->script;
    sb = mb.script[0] ? mb.script : b->script;
    /* If either side still has no script, do not treat as conflict. */
    if (!sa[0] || !sb[0]) {
        return 0;
    }
    return strcmp(sa, sb) != 0;
}

static int RegionsCompatible(const I18nLocale *want, const I18nLocale *have)
{
    if (!want->region[0] || !have->region[0]) {
        return 0;
    }
    return strcmp(want->region, have->region) == 0;
}

static int ScriptsCompatibleExplicit(const I18nLocale *want,
                                       const I18nLocale *have)
{
    if (!want->script[0] || !have->script[0]) {
        return 0;
    }
    return strcmp(want->script, have->script) == 0;
}

I18nNegotiateKind I18nLocaleMatchQuality(
    const I18nLanguageRange *range,
    const I18nLocale *supported)
{
    I18nLocale want;
    I18nLocale have;

    if (!range || !supported) {
        return RoboI18nNegNone;
    }
    if (range->isWildcard) {
        return RoboI18nNegWildcard;
    }
    if (!range->locale.language[0] || !supported->language[0]) {
        return RoboI18nNegNone;
    }
    if (strcmp(range->locale.language, supported->language) != 0) {
        return RoboI18nNegNone;
    }

    want = range->locale;
    have = *supported;
    (void)I18nLocaleCanonicalize(&want);
    (void)I18nLocaleCanonicalize(&have);

    if (ScriptsConflict(&want, &have)) {
        return RoboI18nNegNone;
    }

    if (I18nLocaleEqual(&want, &have)) {
        return RoboI18nNegExact;
    }

    /* Strip extensions/private for equality-ish exact of core fields */
    {
        I18nLocale w = want;
        I18nLocale h = have;
        w.extensions[0] = '\0';
        w.privateuse[0] = '\0';
        h.extensions[0] = '\0';
        h.privateuse[0] = '\0';
        if (I18nLocaleEqual(&w, &h)) {
            return RoboI18nNegExact;
        }
    }

    if (RegionsCompatible(&want, &have)) {
        return RoboI18nNegRegion;
    }
    if (ScriptsCompatibleExplicit(&want, &have)) {
        return RoboI18nNegScript;
    }
    /* Maximized script agreement with explicit request script */
    if (want.script[0]) {
        I18nLocale mh;
        if (I18nLocaleMaximize(&have, &mh) == RoboOk && mh.script[0] &&
            strcmp(want.script, mh.script) == 0) {
            return RoboI18nNegScript;
        }
    }
    return RoboI18nNegLanguage;
}

void I18nPreferenceListDestroy(I18nPreferenceList *list)
{
    if (!list) {
        return;
    }
    RoboFree(list->items);
    RoboFree(list);
}

size_t I18nPreferenceListSize(const I18nPreferenceList *list)
{
    return list ? list->count : 0u;
}

Result I18nPreferenceListGet(const I18nPreferenceList *list,
                                            size_t index,
                                            I18nLanguageRange *out)
{
    if (!list || !out || index >= list->count) {
        return RoboInvalidArgument;
    }
    *out = list->items[index];
    return RoboOk;
}

static Result PrefPush(I18nPreferenceList *list,
                               const I18nLanguageRange *r)
{
    if (list->count >= RoboI18nPrefMaxRanges) {
        return RoboError;
    }
    if (list->count == list->capacity) {
        size_t nc = list->capacity ? list->capacity * 2u : 4u;
        void *ni;
        if (nc > RoboI18nPrefMaxRanges) {
            nc = RoboI18nPrefMaxRanges;
        }
        ni = Realloc(list->items, nc * sizeof(I18nLanguageRange));
        if (!ni) {
            return RoboOutOfMemory;
        }
        list->items = (I18nLanguageRange *)ni;
        list->capacity = nc;
    }
    list->items[list->count++] = *r;
    return RoboOk;
}

static int RangeSameIdentity(const I18nLanguageRange *a,
                               const I18nLanguageRange *b)
{
    if (a->isWildcard != b->isWildcard) {
        return 0;
    }
    if (a->isWildcard) {
        return 1;
    }
    return I18nLocaleEqual(&a->locale, &b->locale);
}

Result I18nPreferenceListNormalize(I18nPreferenceList *list)
{
    size_t i;
    size_t w;
    size_t *order = NULL;
    I18nLanguageRange *tmp = NULL;

    if (!list) {
        return RoboInvalidArgument;
    }
    if (list->count <= 1u) {
        return RoboOk;
    }

    /* Highest-q wins for duplicates; equal q → first-wins (scan forward). */
    w = 0;
    for (i = 0; i < list->count; i++) {
        size_t j;
        int keep = 1;
        for (j = 0; j < w; j++) {
            if (RangeSameIdentity(&list->items[i], &list->items[j])) {
                if (list->items[i].qMilli > list->items[j].qMilli) {
                    list->items[j] = list->items[i];
                }
                keep = 0;
                break;
            }
        }
        if (keep) {
            list->items[w++] = list->items[i];
        }
    }
    list->count = w;
    if (list->count <= 1u) {
        return RoboOk;
    }

    order = (size_t *)Malloc(list->count * sizeof(size_t));
    tmp = (I18nLanguageRange *)Malloc(list->count *
                                                    sizeof(I18nLanguageRange));
    if (!order || !tmp) {
        RoboFree(order);
        RoboFree(tmp);
        return RoboOutOfMemory;
    }
    for (i = 0; i < list->count; i++) {
        order[i] = i;
    }
    /* Stable insertion sort by q descending. */
    for (i = 1; i < list->count; i++) {
        size_t key = order[i];
        size_t b = i;
        while (b > 0 && list->items[order[b - 1]].qMilli < list->items[key].qMilli) {
            order[b] = order[b - 1];
            b--;
        }
        order[b] = key;
    }
    for (i = 0; i < list->count; i++) {
        tmp[i] = list->items[order[i]];
    }
    memcpy(list->items, tmp, list->count * sizeof(I18nLanguageRange));
    RoboFree(order);
    RoboFree(tmp);
    return RoboOk;
}

Result I18nAcceptLanguageParse(const char *text,
                                              size_t len,
                                              I18nPreferenceList **out)
{
    I18nPreferenceList *list;
    size_t i = 0;

    if (!text || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (len > RoboI18nAcceptLangMaxBytes) {
        return RoboError;
    }
    if (len > 0 && !Utf8OkAsciiish(text, len)) {
        return RoboParseError;
    }

    list = (I18nPreferenceList *)RoboCalloc(1, sizeof(*list));
    if (!list) {
        return RoboOutOfMemory;
    }

    while (i < len) {
        size_t start;
        size_t end;
        I18nLanguageRange range;
        Result r;

        while (i < len && (text[i] == ' ' || text[i] == '\t' || text[i] == ',')) {
            i++;
        }
        if (i >= len) {
            break;
        }
        start = i;
        while (i < len && text[i] != ',') {
            i++;
        }
        end = i;
        if (i < len && text[i] == ',') {
            i++;
        }
        while (end > start && IsAsciiWs((unsigned char)text[end - 1])) {
            end--;
        }
        if (end <= start) {
            continue;
        }
        r = I18nLanguageRangeParse(text + start, end - start, &range);
        if (r != RoboOk) {
            continue; /* skip invalid entries */
        }
        r = PrefPush(list, &range);
        if (r != RoboOk) {
            I18nPreferenceListDestroy(list);
            return r;
        }
    }

    *out = list;
    return RoboOk;
}

static size_t AppendStr(char *buf, size_t cap, size_t pos, const char *s)
{
    size_t n;
    if (!s) {
        return pos;
    }
    n = strlen(s);
    if (pos + n >= cap) {
        return (size_t)-1;
    }
    memcpy(buf + pos, s, n);
    return pos + n;
}

static size_t AppendQ(char *buf, size_t cap, size_t pos, I18nQMilli q)
{
    char tmp[8];
    size_t n = 0;
    unsigned whole = q / 1000u;
    unsigned frac = q % 1000u;
    if (whole > 1u) {
        whole = 1u;
        frac = 0u;
    }
    tmp[n++] = (char)('0' + (char)whole);
    if (frac != 0u) {
        tmp[n++] = '.';
        tmp[n++] = (char)('0' + (frac / 100u) % 10u);
        if (frac % 100u != 0u) {
            tmp[n++] = (char)('0' + (frac / 10u) % 10u);
            if (frac % 10u != 0u) {
                tmp[n++] = (char)('0' + frac % 10u);
            }
        }
    }
    tmp[n] = '\0';
    return AppendStr(buf, cap, pos, tmp);
}

Result I18nAcceptLanguageSerialize(const I18nPreferenceList *list,
                                                  char *buf,
                                                  size_t bufCap,
                                                  size_t *outLen)
{
    size_t i;
    size_t pos = 0;

    if (!list || !buf || bufCap == 0) {
        return RoboInvalidArgument;
    }
    buf[0] = '\0';
    for (i = 0; i < list->count; i++) {
        const I18nLanguageRange *r = &list->items[i];
        char tag[RoboI18nTagMax];
        size_t tlen = 0;

        if (i > 0) {
            pos = AppendStr(buf, bufCap, pos, ",");
            if (pos == (size_t)-1) {
                return RoboError;
            }
        }
        if (r->isWildcard) {
            pos = AppendStr(buf, bufCap, pos, "*");
        } else {
            if (I18nLocaleFormat(&r->locale, tag, sizeof(tag), &tlen) != RoboOk) {
                return RoboError;
            }
            pos = AppendStr(buf, bufCap, pos, tag);
        }
        if (pos == (size_t)-1) {
            return RoboError;
        }
        if (r->qMilli != RoboI18nQMilliDefault || r->qExplicit) {
            if (r->qMilli != RoboI18nQMilliDefault) {
                pos = AppendStr(buf, bufCap, pos, ";q=");
                if (pos == (size_t)-1) {
                    return RoboError;
                }
                pos = AppendQ(buf, bufCap, pos, r->qMilli);
                if (pos == (size_t)-1) {
                    return RoboError;
                }
            }
        }
        buf[pos] = '\0';
    }
    if (outLen) {
        *outLen = pos;
    }
    return RoboOk;
}

Result I18nPreferenceListFromLocales(const I18nLocale *locales,
                                                     size_t count,
                                                     I18nPreferenceList **out)
{
    I18nPreferenceList *list;
    size_t i;

    if (!out || (count > 0 && !locales)) {
        return RoboInvalidArgument;
    }
    if (count > RoboI18nPrefMaxRanges) {
        return RoboError;
    }
    *out = NULL;
    list = (I18nPreferenceList *)RoboCalloc(1, sizeof(*list));
    if (!list) {
        return RoboOutOfMemory;
    }
    for (i = 0; i < count; i++) {
        I18nLanguageRange r;
        I18nLanguageRangeClear(&r);
        r.locale = locales[i];
        (void)I18nLocaleCanonicalize(&r.locale);
        if (PrefPush(list, &r) != RoboOk) {
            I18nPreferenceListDestroy(list);
            return RoboOutOfMemory;
        }
    }
    *out = list;
    return RoboOk;
}

Result I18nPreferenceListFromLocaleList(
    const I18nLocaleList *src,
    I18nPreferenceList **out)
{
    I18nPreferenceList *list;
    size_t i;
    size_t n;

    if (!src || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    n = I18nLocaleListSize(src);
    if (n > RoboI18nPrefMaxRanges) {
        return RoboError;
    }
    list = (I18nPreferenceList *)RoboCalloc(1, sizeof(*list));
    if (!list) {
        return RoboOutOfMemory;
    }
    for (i = 0; i < n; i++) {
        I18nLanguageRange r;
        uint16_t q = RoboI18nQMilliDefault;
        I18nLanguageRangeClear(&r);
        if (I18nLocaleListGet(src, i, &r.locale) != RoboOk) {
            I18nPreferenceListDestroy(list);
            return RoboInvalidArgument;
        }
        (void)I18nLocaleCanonicalize(&r.locale);
        (void)I18nLocaleListGetQMilli(src, i, &q);
        r.qMilli = q;
        r.qExplicit = (q != RoboI18nQMilliDefault) ? 1u : 0u;
        if (PrefPush(list, &r) != RoboOk) {
            I18nPreferenceListDestroy(list);
            return RoboOutOfMemory;
        }
    }
    *out = list;
    return RoboOk;
}

static int IsExcluded(const I18nPreferenceList *prefs,
                       const I18nLocale *supported)
{
    size_t i;
    for (i = 0; i < prefs->count; i++) {
        const I18nLanguageRange *r = &prefs->items[i];
        if (r->qMilli != 0) {
            continue;
        }
        if (r->isWildcard) {
            return 1;
        }
        if (I18nLocaleMatchQuality(r, supported) != RoboI18nNegNone) {
            return 1;
        }
    }
    return 0;
}

Result I18nLocaleNegotiateEx(const I18nPreferenceList *prefs,
                                            const I18nLocale *supported,
                                            size_t supportedCount,
                                            I18nNegotiateResult *out)
{
    size_t pi;
    size_t si;
    I18nLocale canon[RoboI18nSupportedMax];

    if (!prefs || !out || (supportedCount > 0 && !supported)) {
        return RoboInvalidArgument;
    }
    if (supportedCount == 0 || supportedCount > RoboI18nSupportedMax) {
        return RoboInvalidArgument;
    }
    I18nNegotiateResultClear(out);

    for (si = 0; si < supportedCount; si++) {
        canon[si] = supported[si];
        (void)I18nLocaleCanonicalize(&canon[si]);
    }

    for (pi = 0; pi < prefs->count; pi++) {
        const I18nLanguageRange *pref = &prefs->items[pi];
        I18nNegotiateKind bestKind = RoboI18nNegNone;
        size_t bestSi = 0;
        int found = 0;

        if (pref->qMilli == 0) {
            continue; /* exclusion only */
        }

        if (pref->isWildcard) {
            for (si = 0; si < supportedCount; si++) {
                if (!IsExcluded(prefs, &canon[si])) {
                    out->selected = canon[si];
                    I18nLocaleClear(&out->requested);
                    out->kind = RoboI18nNegWildcard;
                    out->preferenceIndex = pi;
                    out->qMilli = pref->qMilli;
                    return RoboOk;
                }
            }
            continue;
        }

        for (si = 0; si < supportedCount; si++) {
            I18nNegotiateKind k;
            if (IsExcluded(prefs, &canon[si])) {
                continue;
            }
            k = I18nLocaleMatchQuality(pref, &canon[si]);
            if (k == RoboI18nNegNone) {
                continue;
            }
            /* Lower enum value among EXACT..LANGUAGE is better (EXACT=1 best). */
            if (!found || (int)k < (int)bestKind) {
                bestKind = k;
                bestSi = si;
                found = 1;
                if (k == RoboI18nNegExact) {
                    break;
                }
            }
        }

        if (found) {
            out->selected = canon[bestSi];
            out->requested = pref->locale;
            out->kind = bestKind;
            out->preferenceIndex = pi;
            out->qMilli = pref->qMilli;
            return RoboOk;
        }
    }

    return RoboNotFound;
}
