/*
 * RoboI18N locale implementation — BCP 47 / RFC 5646 (Goal 4.1).
 * Independent of Chromium/ICU. Preferred-Value tables from IANA registry.
 */

#include "RoboI18N/RoboI18NLocale.h"

#include "RoboI18N/RoboIanaData.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

static I18nLocale gDefaultLocale;
static int gDefaultReady;

static int IsAlpha(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int IsDigit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static int IsAlnum(unsigned char c)
{
    return IsAlpha(c) || IsDigit(c);
}

static char ToLower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static char ToUpper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static void CopyLower(char *dst, size_t dstCap, const char *src, size_t n)
{
    size_t i;
    if (n >= dstCap) {
        n = dstCap - 1u;
    }
    for (i = 0; i < n; i++) {
        dst[i] = ToLower(src[i]);
    }
    dst[n] = '\0';
}

static void CopyUpper(char *dst, size_t dstCap, const char *src, size_t n)
{
    size_t i;
    if (n >= dstCap) {
        n = dstCap - 1u;
    }
    for (i = 0; i < n; i++) {
        dst[i] = ToUpper(src[i]);
    }
    dst[n] = '\0';
}

static void CopyTitle(char *dst, size_t dstCap, const char *src, size_t n)
{
    size_t i;
    if (n >= dstCap) {
        n = dstCap - 1u;
    }
    for (i = 0; i < n; i++) {
        dst[i] = (i == 0) ? ToUpper(src[i]) : ToLower(src[i]);
    }
    dst[n] = '\0';
}

static int StreqCi(const char *a, const char *b)
{
    size_t i;
    if (!a || !b) {
        return a == b;
    }
    for (i = 0; a[i] && b[i]; i++) {
        if (ToLower(a[i]) != ToLower(b[i])) {
            return 0;
        }
    }
    return a[i] == '\0' && b[i] == '\0';
}

void I18nLocaleClear(I18nLocale *loc)
{
    if (!loc) {
        return;
    }
    memset(loc, 0, sizeof(*loc));
}

int I18nLocaleHasScript(const I18nLocale *loc)
{
    return loc && loc->script[0] != '\0';
}

int I18nLocaleHasRegion(const I18nLocale *loc)
{
    return loc && loc->region[0] != '\0';
}

const char *I18nLocaleLanguage(const I18nLocale *loc)
{
    return loc ? loc->language : "";
}

const char *I18nLocaleScript(const I18nLocale *loc)
{
    return loc ? loc->script : "";
}

const char *I18nLocaleRegion(const I18nLocale *loc)
{
    return loc ? loc->region : "";
}

static int AllAlpha(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (!IsAlpha((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static int AllDigit(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (!IsDigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static int AllAlnum(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (!IsAlnum((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static Result ParseCore(const char *tag, size_t len, I18nLocale *out);

static Result TryReplaceFullTag(const char *tag,
                                          size_t len,
                                          I18nLocale *out)
{
    char key[RoboI18nTagMax + 1u];
    const char *pref;
    size_t i;

    if (len == 0 || len > RoboI18nTagMax) {
        return RoboParseError;
    }
    for (i = 0; i < len; i++) {
        key[i] = ToLower(tag[i]);
    }
    key[len] = '\0';

    pref = IanaLookup(IanaGrandfathered, IanaGrandfatheredCount, key);
    if (!pref) {
        pref = IanaLookup(IanaRedundant, IanaRedundantCount, key);
    }
    if (!pref) {
        return RoboNotFound;
    }
    return ParseCore(pref, strlen(pref), out);
}

static Result ParseCore(const char *tag, size_t len, I18nLocale *out)
{
    size_t pos = 0;
    size_t start;
    size_t n;

    I18nLocaleClear(out);
    if (!tag || !out) {
        return RoboInvalidArgument;
    }
    if (len == 0) {
        return RoboParseError;
    }

    /* Primary language: 2-8 ALPHA */
    start = pos;
    while (pos < len && tag[pos] != '-') {
        pos++;
    }
    n = pos - start;
    if (n < 2u || n > 8u || !AllAlpha(tag + start, n)) {
        return RoboParseError;
    }
    if (n > RoboI18nLangMax) {
        return RoboParseError;
    }
    CopyLower(out->language, sizeof(out->language), tag + start, n);

    while (pos < len) {
        if (tag[pos] != '-') {
            return RoboParseError;
        }
        pos++;
        if (pos >= len) {
            return RoboParseError; /* empty subtag */
        }
        start = pos;
        while (pos < len && tag[pos] != '-') {
            pos++;
        }
        n = pos - start;
        if (n == 0) {
            return RoboParseError;
        }

        /* Private use: x / X singleton */
        if (n == 1 && (tag[start] == 'x' || tag[start] == 'X')) {
            size_t rem = len - start;
            if (rem > RoboI18nExtMax) {
                return RoboParseError;
            }
            CopyLower(out->privateuse, sizeof(out->privateuse), tag + start, rem);
            return RoboOk;
        }

        /* Extension singleton (other than x): one ALPHA */
        if (n == 1 && IsAlpha((unsigned char)tag[start])) {
            size_t rem;
            rem = len - start;
            if (rem > RoboI18nExtMax) {
                return RoboParseError;
            }
            {
                size_t p = start + 1;
                if (p >= len || tag[p] != '-') {
                    return RoboParseError;
                }
                p++;
                while (p < len) {
                    size_t s = p;
                    size_t sn;
                    while (p < len && tag[p] != '-') {
                        p++;
                    }
                    sn = p - s;
                    if (sn < 2u || sn > 8u || !AllAlnum(tag + s, sn)) {
                        return RoboParseError;
                    }
                    if (p < len) {
                        p++;
                    }
                }
            }
            CopyLower(out->extensions, sizeof(out->extensions), tag + start, rem);
            return RoboOk;
        }

        /* Extlang: 3 ALPHA after language, before script/region (up to 3) — fold into language if empty script/region */
        if (out->script[0] == '\0' && out->region[0] == '\0' && out->variantCount == 0 &&
            n == 3u && AllAlpha(tag + start, n) && strlen(out->language) <= 3u) {
            /* Keep primary language; extlang replaced via Preferred-Value if registered as language later.
             * Structurally accept and store first extlang by appending is non-standard.
             * RFC: extlang is a separate subtag. For locale model we keep primary only and
             * treat extlang as requiring Preferred-Value at canonicalize for known cases.
             * Store as additional language detail: if language is 2-3 letters, replace with
             * preferred of extlang when available at canonicalize; for now accept by ignoring
             * extlang content beyond validation — actually better: set language to extlang when
             * IANA says so. Simplest structural: allow up to one extlang stored in a side path.
             * Goal 4.1: skip storing extlang; reject if we already have language and next is
             * 3-letter AND following isn't clearly script/region — wait, 3-letter region is digits.
             * 3 ALPHA after language = extlang. We'll apply language Preferred-Value for that
             * subtag alone: if preferred exists, replace language. */
            {
                char ext[4];
                const char *pref;
                CopyLower(ext, sizeof(ext), tag + start, 3);
                pref = IanaLookup(IanaLangPref, IanaLangPrefCount, ext);
                if (pref && strlen(pref) <= RoboI18nLangMax && strchr(pref, '-') == NULL) {
                    CopyLower(out->language, sizeof(out->language), pref, strlen(pref));
                }
                /* If no preferred, keep primary language (extlang unknown) — still valid. */
            }
            continue;
        }

        /* Script: 4 ALPHA */
        if (out->script[0] == '\0' && out->region[0] == '\0' && out->variantCount == 0 &&
            n == 4u && AllAlpha(tag + start, n)) {
            CopyTitle(out->script, sizeof(out->script), tag + start, n);
            continue;
        }

        /* Region: 2 ALPHA or 3 DIGIT */
        if (out->region[0] == '\0' && out->variantCount == 0 &&
            ((n == 2u && AllAlpha(tag + start, n)) || (n == 3u && AllDigit(tag + start, n)))) {
            if (n == 2u) {
                CopyUpper(out->region, sizeof(out->region), tag + start, n);
            } else {
                memcpy(out->region, tag + start, n);
                out->region[n] = '\0';
            }
            continue;
        }

        /* Variant: 5-8 alphanum, or DIGIT + 3 alphanum */
        if ((n >= 5u && n <= 8u && AllAlnum(tag + start, n)) ||
            (n == 4u && IsDigit((unsigned char)tag[start]) && AllAlnum(tag + start, n))) {
            if (out->variantCount >= RoboI18nVariantCount) {
                return RoboParseError;
            }
            CopyLower(out->variants[out->variantCount],
                       sizeof(out->variants[0]),
                       tag + start,
                       n);
            out->variantCount++;
            continue;
        }

        return RoboParseError;
    }

    return RoboOk;
}

Result I18nLocaleParse(const char *tag, size_t len, I18nLocale *out)
{
    Result r;

    if (!tag || !out) {
        return RoboInvalidArgument;
    }
    r = TryReplaceFullTag(tag, len, out);
    if (r == RoboOk) {
        return RoboOk;
    }
    if (r != RoboNotFound) {
        return r;
    }
    return ParseCore(tag, len, out);
}

Result I18nLocaleCanonicalize(I18nLocale *loc)
{
    const char *pref;
    uint8_t i;

    if (!loc || loc->language[0] == '\0') {
        return RoboInvalidArgument;
    }

    pref = IanaLookup(IanaLangPref, IanaLangPrefCount, loc->language);
    if (pref) {
        if (strchr(pref, '-')) {
            /* Preferred language range — re-parse */
            return I18nLocaleParse(pref, strlen(pref), loc);
        }
        if (strlen(pref) <= RoboI18nLangMax) {
            CopyLower(loc->language, sizeof(loc->language), pref, strlen(pref));
        }
    }

    if (loc->script[0]) {
        char key[5];
        CopyLower(key, sizeof(key), loc->script, strlen(loc->script));
        pref = IanaLookup(IanaScriptPref, IanaScriptPrefCount, key);
        if (pref && strlen(pref) == 4u) {
            CopyTitle(loc->script, sizeof(loc->script), pref, 4u);
        }
    }

    if (loc->region[0]) {
        char key[4];
        if (IsAlpha((unsigned char)loc->region[0])) {
            CopyUpper(key, sizeof(key), loc->region, strlen(loc->region));
        } else {
            memcpy(key, loc->region, sizeof(key));
        }
        pref = IanaLookup(IanaRegionPref, IanaRegionPrefCount, key);
        if (pref) {
            if (IsAlpha((unsigned char)pref[0])) {
                CopyUpper(loc->region, sizeof(loc->region), pref, strlen(pref));
            } else {
                size_t n = strlen(pref);
                if (n <= RoboI18nRegionMax) {
                    memcpy(loc->region, pref, n + 1u);
                }
            }
        }
    }

    for (i = 0; i < loc->variantCount; i++) {
        pref = IanaLookup(
            IanaVariantPref, IanaVariantPrefCount, loc->variants[i]);
        if (pref && strlen(pref) <= RoboI18nVariantMax) {
            CopyLower(loc->variants[i], sizeof(loc->variants[i]), pref, strlen(pref));
        }
    }

    return RoboOk;
}

Result I18nLocaleParseCanonical(const char *tag,
                                               size_t len,
                                               I18nLocale *out)
{
    Result r = I18nLocaleParse(tag, len, out);
    if (r != RoboOk) {
        return r;
    }
    return I18nLocaleCanonicalize(out);
}

Result I18nLocaleFormat(const I18nLocale *loc,
                                      char *buf,
                                      size_t bufCap,
                                      size_t *outLen)
{
    char tmp[RoboI18nTagMax + 1u];
    size_t n = 0;
    uint8_t i;

    if (!loc || !buf || bufCap == 0) {
        return RoboInvalidArgument;
    }
    if (loc->language[0] == '\0') {
        return RoboInvalidArgument;
    }

    n = strlen(loc->language);
    if (n >= sizeof(tmp)) {
        return RoboParseError;
    }
    memcpy(tmp, loc->language, n);

    if (loc->script[0]) {
        tmp[n++] = '-';
        memcpy(tmp + n, loc->script, 4u);
        n += 4u;
    }
    if (loc->region[0]) {
        size_t rn = strlen(loc->region);
        tmp[n++] = '-';
        memcpy(tmp + n, loc->region, rn);
        n += rn;
    }
    for (i = 0; i < loc->variantCount; i++) {
        size_t vn = strlen(loc->variants[i]);
        if (n + 1u + vn >= sizeof(tmp)) {
            return RoboParseError;
        }
        tmp[n++] = '-';
        memcpy(tmp + n, loc->variants[i], vn);
        n += vn;
    }
    if (loc->extensions[0]) {
        size_t en = strlen(loc->extensions);
        if (n + 1u + en >= sizeof(tmp)) {
            return RoboParseError;
        }
        tmp[n++] = '-';
        memcpy(tmp + n, loc->extensions, en);
        n += en;
    }
    if (loc->privateuse[0]) {
        size_t pn = strlen(loc->privateuse);
        if (n + 1u + pn >= sizeof(tmp)) {
            return RoboParseError;
        }
        tmp[n++] = '-';
        memcpy(tmp + n, loc->privateuse, pn);
        n += pn;
    }
    tmp[n] = '\0';

    if (n + 1u > bufCap) {
        return RoboInvalidArgument;
    }
    memcpy(buf, tmp, n + 1u);
    if (outLen) {
        *outLen = n;
    }
    return RoboOk;
}

int I18nLocaleEqual(const I18nLocale *a, const I18nLocale *b)
{
    uint8_t i;
    if (!a || !b) {
        return 0;
    }
    if (!StreqCi(a->language, b->language)) {
        return 0;
    }
    if (!StreqCi(a->script, b->script)) {
        return 0;
    }
    if (!StreqCi(a->region, b->region)) {
        return 0;
    }
    if (a->variantCount != b->variantCount) {
        return 0;
    }
    for (i = 0; i < a->variantCount; i++) {
        if (!StreqCi(a->variants[i], b->variants[i])) {
            return 0;
        }
    }
    if (!StreqCi(a->extensions, b->extensions)) {
        return 0;
    }
    if (!StreqCi(a->privateuse, b->privateuse)) {
        return 0;
    }
    return 1;
}

I18nMatch I18nLocaleMatches(const I18nLocale *range,
                                           const I18nLocale *candidate)
{
    if (!range || !candidate) {
        return RoboI18nMatchNone;
    }
    if (!StreqCi(range->language, candidate->language)) {
        return RoboI18nMatchNone;
    }
    if (range->script[0] && !StreqCi(range->script, candidate->script)) {
        return RoboI18nMatchNone;
    }
    if (range->region[0] && !StreqCi(range->region, candidate->region)) {
        return RoboI18nMatchNone;
    }
    if (I18nLocaleEqual(range, candidate)) {
        return RoboI18nMatchExact;
    }
    if (range->region[0]) {
        return RoboI18nMatchRegion;
    }
    if (range->script[0]) {
        return RoboI18nMatchScript;
    }
    return RoboI18nMatchLanguage;
}

Result I18nLocaleParent(const I18nLocale *loc, I18nLocale *out)
{
    if (!loc || !out) {
        return RoboInvalidArgument;
    }
    *out = *loc;

    if (out->privateuse[0]) {
        out->privateuse[0] = '\0';
        return RoboOk;
    }
    if (out->extensions[0]) {
        out->extensions[0] = '\0';
        return RoboOk;
    }
    if (out->variantCount > 0) {
        out->variantCount--;
        out->variants[out->variantCount][0] = '\0';
        return RoboOk;
    }
    if (out->region[0]) {
        out->region[0] = '\0';
        return RoboOk;
    }
    if (out->script[0]) {
        out->script[0] = '\0';
        return RoboOk;
    }
    return RoboNotFound;
}

Result I18nSetDefaultLocale(const I18nLocale *loc)
{
    if (!loc || loc->language[0] == '\0') {
        return RoboInvalidArgument;
    }
    gDefaultLocale = *loc;
    gDefaultReady = 1;
    return RoboOk;
}

Result I18nGetDefaultLocale(I18nLocale *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gDefaultReady) {
        /* Deterministic default: undefined language (not en-US). */
        I18nLocaleClear(out);
        memcpy(out->language, "und", 4);
        return RoboOk;
    }
    *out = gDefaultLocale;
    return RoboOk;
}

/* --- locale lists --- */

struct I18nLocaleList {
    I18nLocale *items;
    float *weights;
    size_t count;
    size_t capacity;
};

void I18nLocaleListDestroy(I18nLocaleList *list)
{
    if (!list) {
        return;
    }
    RoboFree(list->items);
    RoboFree(list->weights);
    RoboFree(list);
}

size_t I18nLocaleListSize(const I18nLocaleList *list)
{
    return list ? list->count : 0u;
}

Result I18nLocaleListGet(const I18nLocaleList *list,
                                        size_t index,
                                        I18nLocale *out)
{
    if (!list || !out || index >= list->count) {
        return RoboInvalidArgument;
    }
    *out = list->items[index];
    return RoboOk;
}

Result I18nLocaleListGetQMilli(const I18nLocaleList *list,
                                                size_t index,
                                                uint16_t *outQMilli)
{
    float w;
    unsigned m;
    if (!list || !outQMilli || index >= list->count) {
        return RoboInvalidArgument;
    }
    if (!list->weights) {
        *outQMilli = 1000u;
        return RoboOk;
    }
    w = list->weights[index];
    if (w <= 0.0f) {
        *outQMilli = 0u;
        return RoboOk;
    }
    if (w >= 1.0f) {
        *outQMilli = 1000u;
        return RoboOk;
    }
    m = (unsigned)(w * 1000.0f + 0.5f);
    if (m > 1000u) {
        m = 1000u;
    }
    *outQMilli = (uint16_t)m;
    return RoboOk;
}

static float ParseQ(const char *s, size_t len)
{
    /* Parse q-value in [0,1] without libc float conversion. */
    size_t i = 0;
    float v = 0.0f;
    float scale = 0.1f;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    if (i >= len) {
        return 1.0f;
    }
    if (s[i] == '1') {
        return 1.0f;
    }
    if (s[i] == '0') {
        i++;
        if (i < len && s[i] == '.') {
            i++;
            while (i < len && IsDigit((unsigned char)s[i])) {
                v += (float)(s[i] - '0') * scale;
                scale *= 0.1f;
                i++;
            }
        }
        if (v > 1.0f) {
            v = 1.0f;
        }
        return v;
    }
    return 1.0f;
}
Result I18nLocaleListParse(const char *text,
                                          size_t len,
                                          I18nLocaleList **out)
{
    I18nLocaleList *list;
    size_t i = 0;
    size_t *order = NULL;
    I18nLocale *sorted = NULL;
    float *sortedW = NULL;

    if (!text || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    list = (I18nLocaleList *)RoboCalloc(1, sizeof(*list));
    if (!list) {
        return RoboOutOfMemory;
    }

    while (i < len) {
        size_t start;
        size_t end;
        size_t tagEnd;
        float q = 1.0f;
        I18nLocale loc;
        Result r;

        while (i < len && (text[i] == ' ' || text[i] == '\t')) {
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
        while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
            end--;
        }
        if (end <= start) {
            continue;
        }

        tagEnd = start;
        while (tagEnd < end && text[tagEnd] != ';') {
            tagEnd++;
        }
        {
            size_t te = tagEnd;
            while (te > start && (text[te - 1] == ' ' || text[te - 1] == '\t')) {
                te--;
            }
            tagEnd = te;
        }
        if (tagEnd > start && tagEnd < end && text[tagEnd] != ';') {
            /* trimmed */
        }
        if (tagEnd < end && text[tagEnd] == ';') {
            /* parse q= */
            size_t p = tagEnd + 1;
            while (p < end && (text[p] == ' ' || text[p] == '\t')) {
                p++;
            }
            if (p + 2 < end && (text[p] == 'q' || text[p] == 'Q') && text[p + 1] == '=') {
                q = ParseQ(text + p + 2, end - (p + 2));
            }
        } else {
            /* no semicolon — tagEnd may equal end */
        }

        /* Fix tag length when semicolon present */
        {
            size_t tlen;
            size_t te = start;
            while (te < end && text[te] != ';') {
                te++;
            }
            while (te > start && (text[te - 1] == ' ' || text[te - 1] == '\t')) {
                te--;
            }
            tlen = te - start;
            if (tlen == 0) {
                continue;
            }
            r = I18nLocaleParseCanonical(text + start, tlen, &loc);
            if (r != RoboOk) {
                I18nLocaleListDestroy(list);
                return r;
            }
        }

        if (list->count == list->capacity) {
            size_t nc = list->capacity ? list->capacity * 2u : 4u;
            void *ni = Realloc(list->items, nc * sizeof(I18nLocale));
            void *nw = Realloc(list->weights, nc * sizeof(float));
            if (!ni || !nw) {
                RoboFree(ni);
                RoboFree(nw);
                I18nLocaleListDestroy(list);
                return RoboOutOfMemory;
            }
            list->items = (I18nLocale *)ni;
            list->weights = (float *)nw;
            list->capacity = nc;
        }
        list->items[list->count] = loc;
        list->weights[list->count] = q;
        list->count++;
    }

    if (list->count > 1u) {
        order = (size_t *)Malloc(list->count * sizeof(size_t));
        sorted = (I18nLocale *)Malloc(list->count * sizeof(I18nLocale));
        sortedW = (float *)Malloc(list->count * sizeof(float));
        if (!order || !sorted || !sortedW) {
            RoboFree(order);
            RoboFree(sorted);
            RoboFree(sortedW);
            I18nLocaleListDestroy(list);
            return RoboOutOfMemory;
        }
        for (i = 0; i < list->count; i++) {
            order[i] = i;
        }
        {
            size_t a, b;
            for (a = 1; a < list->count; a++) {
                size_t key = order[a];
                b = a;
                while (b > 0 && list->weights[order[b - 1]] < list->weights[key]) {
                    order[b] = order[b - 1];
                    b--;
                }
                order[b] = key;
            }
        }
        for (i = 0; i < list->count; i++) {
            sorted[i] = list->items[order[i]];
            sortedW[i] = list->weights[order[i]];
        }
        RoboFree(list->items);
        RoboFree(list->weights);
        RoboFree(order);
        list->items = sorted;
        list->weights = sortedW;
    }

    *out = list;
    return RoboOk;
}
