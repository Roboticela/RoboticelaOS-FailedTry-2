/*
 * RoboI18N application message catalogs (Goal 4.11).
 */

#include "RoboI18N/RoboI18NMessage.h"

#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboMsgData.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define RoboMsgMagic 0x47534D52u
#define RoboMsgHeaderSize 32u
#define RoboMsgEntrySize 40u

struct I18nCatalog {
    int owned; /* 1 = free blob on close */
    uint32_t formatVersion;
    const char *catalogId;
    const char *catalogVersion;
    /* Demo tables */
    int isDemo;
    /* Loaded blob (owned when owned==1) */
    uint8_t *blob;
    size_t blobLen;
    const uint8_t *pool;
    uint32_t poolSize;
    uint32_t entryCount;
    /* For demo: use MsgEntries. For blob: offsets into pool. */
};

static I18nCatalog gDemoCatalog;
static int gDemoReady;

void I18nMsgOptionsInit(I18nMsgOptions *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->missing = RoboI18nMsgMissNotFound;
    out->varMissing = RoboI18nMsgVarMissError;
}

static const I18nMsgOptions *OptsOrDefault(
    const I18nMsgOptions *options,
    I18nMsgOptions *scratch)
{
    if (options) {
        return options;
    }
    I18nMsgOptionsInit(scratch);
    return scratch;
}

static Result WriteOut(const char *s,
                               size_t n,
                               char *out,
                               size_t outCap,
                               size_t *outLen)
{
    if (!outLen) {
        return RoboInvalidArgument;
    }
    *outLen = n;
    if (!out) {
        return RoboOk;
    }
    if (outCap < n + 1u) {
        *outLen = n + 1u;
        return RoboError;
    }
    if (n) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return RoboOk;
}

static int Utf8Ok(const uint8_t *p, size_t n)
{
    size_t i = 0;
    while (i < n) {
        uint8_t c = p[i];
        if (c < 0x80u) {
            i++;
        } else if ((c >> 5) == 0x6u && i + 1 < n && (p[i + 1] & 0xC0u) == 0x80u) {
            i += 2;
        } else if ((c >> 4) == 0xEu && i + 2 < n && (p[i + 1] & 0xC0u) == 0x80u &&
                   (p[i + 2] & 0xC0u) == 0x80u) {
            i += 3;
        } else if ((c >> 3) == 0x1Eu && i + 3 < n && (p[i + 1] & 0xC0u) == 0x80u &&
                   (p[i + 2] & 0xC0u) == 0x80u && (p[i + 3] & 0xC0u) == 0x80u) {
            i += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

static const char *BlobStr(const I18nCatalog *cat, int32_t off)
{
    size_t o;
    if (!cat || !cat->pool || off < 0) {
        return NULL;
    }
    o = (size_t)(uint32_t)off;
    if (o >= cat->poolSize) {
        return NULL;
    }
    return (const char *)(cat->pool + o);
}

static int CmpBytes(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t n = alen < blen ? alen : blen;
    int c = n ? memcmp(a, b, n) : 0;
    if (c) {
        return c;
    }
    if (alen < blen) {
        return -1;
    }
    if (alen > blen) {
        return 1;
    }
    return 0;
}

static int EntryCmpKey(const char *loc,
                         const char *id,
                         const char *ctx,
                         const char *qloc,
                         size_t qlocLen,
                         const char *qid,
                         size_t qidLen,
                         const char *qctx,
                         size_t qctxLen)
{
    int c;
    if (!loc) {
        loc = "";
    }
    if (!id) {
        id = "";
    }
    if (!ctx) {
        ctx = "";
    }
    c = CmpBytes(loc, strlen(loc), qloc, qlocLen);
    if (c) {
        return c;
    }
    c = CmpBytes(id, strlen(id), qid, qidLen);
    if (c) {
        return c;
    }
    return CmpBytes(ctx, strlen(ctx), qctx, qctxLen);
}

static int FindEntryDemo(const char *loc,
                           size_t locLen,
                           const char *id,
                           size_t idLen,
                           const char *ctx,
                           size_t ctxLen,
                           uint32_t *outIdx)
{
    uint32_t lo = 0;
    uint32_t hi = MsgEntryCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const MsgEntry *e = &MsgEntries[mid];
        const char *el = MsgString(e->locale);
        const char *ei = MsgString(e->id);
        const char *ec = e->context >= 0 ? MsgString(e->context) : "";
        int cmp = EntryCmpKey(el, ei, ec, loc, locLen, id, idLen, ctx, ctxLen);
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo < MsgEntryCount) {
        const MsgEntry *e = &MsgEntries[lo];
        const char *el = MsgString(e->locale);
        const char *ei = MsgString(e->id);
        const char *ec = e->context >= 0 ? MsgString(e->context) : "";
        if (EntryCmpKey(el, ei, ec, loc, locLen, id, idLen, ctx, ctxLen) == 0) {
            *outIdx = lo;
            return 1;
        }
    }
    return 0;
}

static int FindEntryBlob(const I18nCatalog *cat,
                           const char *loc,
                           size_t locLen,
                           const char *id,
                           size_t idLen,
                           const char *ctx,
                           size_t ctxLen,
                           uint32_t *outIdx)
{
    const uint8_t *base;
    uint32_t lo = 0;
    uint32_t hi;
    if (!cat || !cat->blob || cat->entryCount == 0) {
        return 0;
    }
    base = cat->blob + RoboMsgHeaderSize;
    hi = cat->entryCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const int32_t *raw = (const int32_t *)(base + mid * RoboMsgEntrySize);
        const char *el = BlobStr(cat, raw[0]);
        const char *ei = BlobStr(cat, raw[1]);
        const char *ec = raw[2] < 0 ? "" : BlobStr(cat, raw[2]);
        int cmp = EntryCmpKey(el, ei, ec, loc, locLen, id, idLen, ctx, ctxLen);
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo < cat->entryCount) {
        const int32_t *raw = (const int32_t *)(base + lo * RoboMsgEntrySize);
        const char *el = BlobStr(cat, raw[0]);
        const char *ei = BlobStr(cat, raw[1]);
        const char *ec = raw[2] < 0 ? "" : BlobStr(cat, raw[2]);
        if (EntryCmpKey(el, ei, ec, loc, locLen, id, idLen, ctx, ctxLen) == 0) {
            *outIdx = lo;
            return 1;
        }
    }
    return 0;
}

static const char *SelectTextDemo(uint32_t idx,
                                    const I18nLocale *locale,
                                    const I18nDecimal *pluralValue)
{
    const MsgEntry *e = &MsgEntries[idx];
    if (e->text >= 0) {
        return MsgString(e->text);
    }
    if (pluralValue) {
        I18nPluralCategory cat = RoboI18nPluralOther;
        if (I18nPluralCardinal(locale, pluralValue, &cat) == RoboOk) {
            if (cat <= RoboI18nPluralOther && e->plural[cat] >= 0) {
                return MsgString(e->plural[cat]);
            }
        }
    }
    if (e->plural[RoboI18nPluralOther] >= 0) {
        return MsgString(e->plural[RoboI18nPluralOther]);
    }
    return NULL;
}

static const char *SelectTextBlob(const I18nCatalog *cat,
                                    uint32_t idx,
                                    const I18nLocale *locale,
                                    const I18nDecimal *pluralValue)
{
    const uint8_t *base = cat->blob + RoboMsgHeaderSize;
    const int32_t *raw = (const int32_t *)(base + idx * RoboMsgEntrySize);
    if (raw[3] >= 0) {
        return BlobStr(cat, raw[3]);
    }
    if (pluralValue) {
        I18nPluralCategory pc = RoboI18nPluralOther;
        if (I18nPluralCardinal(locale, pluralValue, &pc) == RoboOk) {
            if (pc <= RoboI18nPluralOther && raw[4 + (int)pc] >= 0) {
                return BlobStr(cat, raw[4 + (int)pc]);
            }
        }
    }
    if (raw[4 + RoboI18nPluralOther] >= 0) {
        return BlobStr(cat, raw[4 + RoboI18nPluralOther]);
    }
    return NULL;
}

static int NextParent(I18nLocale *cur)
{
    I18nLocale parent;
    /* Prefer CLDR parentLocales / data inheritance over truncation parent. */
    if (I18nLocaleDataParent(cur, &parent) != RoboOk) {
        return 0;
    }
    if (I18nLocaleEqual(cur, &parent)) {
        return 0;
    }
    *cur = parent;
    return 1;
}

static const char *ResolveMessage(const I18nCatalog *catalog,
                                   const char *messageId,
                                   size_t idLen,
                                   const char *context,
                                   size_t contextLen,
                                   const I18nLocale *locale,
                                   const I18nDecimal *pluralValue,
                                   const I18nMsgOptions *opt,
                                   I18nLocale *usedLocale)
{
    I18nLocale cur;
    char tag[RoboI18nTagMax];
    int guard = 0;
    uint32_t idx = 0;
    const char *ctx = context ? context : "";
    size_t ctxLen = context ? contextLen : 0;

    if (!locale) {
        return NULL;
    }
    cur = *locale;
    for (;;) {
        size_t tlen = 0;
        (void)I18nLocaleFormat(&cur, tag, sizeof(tag), &tlen);
        if (catalog->isDemo) {
            if (FindEntryDemo(tag, tlen, messageId, idLen, ctx, ctxLen, &idx)) {
                if (usedLocale) {
                    *usedLocale = cur;
                }
                return SelectTextDemo(idx, &cur, pluralValue);
            }
        } else {
            if (FindEntryBlob(catalog, tag, tlen, messageId, idLen, ctx, ctxLen,
                                &idx)) {
                if (usedLocale) {
                    *usedLocale = cur;
                }
                return SelectTextBlob(catalog, idx, &cur, pluralValue);
            }
        }
        if (!NextParent(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }

    if (opt->fallbackLocale[0]) {
        I18nLocale fb;
        if (I18nLocaleParseCanonical(opt->fallbackLocale,
                                             strlen(opt->fallbackLocale),
                                             &fb) == RoboOk) {
            cur = fb;
            guard = 0;
            for (;;) {
                size_t tlen = 0;
                (void)I18nLocaleFormat(&cur, tag, sizeof(tag), &tlen);
                if (catalog->isDemo) {
                    if (FindEntryDemo(tag, tlen, messageId, idLen, ctx, ctxLen,
                                        &idx)) {
                        if (usedLocale) {
                            *usedLocale = cur;
                        }
                        return SelectTextDemo(idx, &cur, pluralValue);
                    }
                } else {
                    if (FindEntryBlob(catalog, tag, tlen, messageId, idLen, ctx,
                                        ctxLen, &idx)) {
                        if (usedLocale) {
                            *usedLocale = cur;
                        }
                        return SelectTextBlob(catalog, idx, &cur, pluralValue);
                    }
                }
                if (!NextParent(&cur)) {
                    break;
                }
                if (++guard > 16) {
                    break;
                }
            }
        }
    }
    return NULL;
}

Result I18nCatalogOpenDemo(I18nCatalog **out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gDemoReady) {
        memset(&gDemoCatalog, 0, sizeof(gDemoCatalog));
        gDemoCatalog.isDemo = 1;
        gDemoCatalog.owned = 0;
        gDemoCatalog.formatVersion = RoboMsgDemoFormatVersion;
        gDemoCatalog.catalogId = MsgCatalogId();
        gDemoCatalog.catalogVersion = MsgCatalogVersion();
        gDemoCatalog.entryCount = MsgEntryCount;
        gDemoReady = 1;
    }
    *out = &gDemoCatalog;
    return RoboOk;
}

Result I18nCatalogLoad(const uint8_t *blob,
                                     size_t blobLen,
                                     I18nCatalog **out)
{
    uint32_t magic;
    uint32_t format;
    uint32_t catalogIdOff;
    uint32_t catalogVerOff;
    uint32_t genOff;
    uint32_t localeCount;
    uint32_t entryCount;
    uint32_t poolSize;
    size_t entriesBytes;
    size_t need;
    const uint8_t *pool;
    uint32_t i;
    I18nCatalog *cat;
    uint8_t *copy;

    if (!blob || !out) {
        return RoboInvalidArgument;
    }
    if (blobLen < RoboMsgHeaderSize) {
        return RoboParseError;
    }
    memcpy(&magic, blob + 0, 4);
    memcpy(&format, blob + 4, 4);
    memcpy(&catalogIdOff, blob + 8, 4);
    memcpy(&catalogVerOff, blob + 12, 4);
    memcpy(&genOff, blob + 16, 4);
    memcpy(&localeCount, blob + 20, 4);
    memcpy(&entryCount, blob + 24, 4);
    memcpy(&poolSize, blob + 28, 4);
    (void)localeCount;
    (void)genOff;
    if (magic != RoboMsgMagic || format != RoboMsgFormatVersion) {
        return RoboParseError;
    }
    if (entryCount > 1000000u || poolSize > 64u * 1024u * 1024u) {
        return RoboParseError;
    }
    entriesBytes = (size_t)entryCount * RoboMsgEntrySize;
    need = RoboMsgHeaderSize + entriesBytes + (size_t)poolSize;
    if (need != blobLen || need < RoboMsgHeaderSize) {
        return RoboParseError;
    }
    pool = blob + RoboMsgHeaderSize + entriesBytes;
    if (catalogIdOff >= poolSize || catalogVerOff >= poolSize) {
        return RoboParseError;
    }
    if (!Utf8Ok(pool, poolSize)) {
        return RoboParseError;
    }
    for (i = 0; i < entryCount; i++) {
        const int32_t *raw =
            (const int32_t *)(blob + RoboMsgHeaderSize + i * RoboMsgEntrySize);
        int k;
        for (k = 0; k < 10; k++) {
            int32_t off = raw[k];
            if (off < 0) {
                continue;
            }
            if ((uint32_t)off >= poolSize) {
                return RoboParseError;
            }
        }
    }

    copy = (uint8_t *)Malloc(blobLen);
    if (!copy) {
        return RoboOutOfMemory;
    }
    memcpy(copy, blob, blobLen);
    cat = (I18nCatalog *)RoboCalloc(1, sizeof(*cat));
    if (!cat) {
        RoboFree(copy);
        return RoboOutOfMemory;
    }
    cat->owned = 1;
    cat->isDemo = 0;
    cat->formatVersion = format;
    cat->blob = copy;
    cat->blobLen = blobLen;
    cat->pool = copy + RoboMsgHeaderSize + entriesBytes;
    cat->poolSize = poolSize;
    cat->entryCount = entryCount;
    cat->catalogId = (const char *)(cat->pool + catalogIdOff);
    cat->catalogVersion = (const char *)(cat->pool + catalogVerOff);
    *out = cat;
    return RoboOk;
}

void I18nCatalogClose(I18nCatalog *catalog)
{
    if (!catalog) {
        return;
    }
    if (catalog->owned) {
        RoboFree(catalog->blob);
        catalog->blob = NULL;
        RoboFree(catalog);
    }
}

const char *I18nCatalogId(const I18nCatalog *catalog)
{
    return catalog ? catalog->catalogId : NULL;
}

const char *I18nCatalogVersion(const I18nCatalog *catalog)
{
    return catalog ? catalog->catalogVersion : NULL;
}

uint32_t I18nCatalogFormatVersion(const I18nCatalog *catalog)
{
    return catalog ? catalog->formatVersion : 0u;
}

int I18nMessageExists(const I18nCatalog *catalog,
                             const char *messageId,
                             size_t idLen,
                             const char *context,
                             size_t contextLen,
                             const I18nLocale *locale,
                             const I18nMsgOptions *options)
{
    I18nMsgOptions scratch;
    const I18nMsgOptions *opt;
    const char *t;
    if (!catalog || !messageId || !locale) {
        return 0;
    }
    opt = OptsOrDefault(options, &scratch);
    t = ResolveMessage(catalog, messageId, idLen, context, contextLen, locale, NULL,
                        opt, NULL);
    return t != NULL;
}

Result I18nMessageGet(const I18nCatalog *catalog,
                                    const char *messageId,
                                    size_t idLen,
                                    const char *context,
                                    size_t contextLen,
                                    const I18nLocale *locale,
                                    const I18nDecimal *pluralValue,
                                    const I18nMsgOptions *options,
                                    char *out,
                                    size_t outCap,
                                    size_t *outLen)
{
    I18nMsgOptions scratch;
    const I18nMsgOptions *opt;
    const char *t;
    I18nLocale used;

    if (!catalog || !messageId || !locale || !outLen) {
        return RoboInvalidArgument;
    }
    if (idLen == 0 || idLen > RoboI18nMsgIdMax) {
        return RoboInvalidArgument;
    }
    opt = OptsOrDefault(options, &scratch);
    t = ResolveMessage(catalog, messageId, idLen, context, contextLen, locale,
                        pluralValue, opt, &used);
    if (t) {
        return WriteOut(t, strlen(t), out, outCap, outLen);
    }
    if (opt->missing == RoboI18nMsgMissReturnId) {
        return WriteOut(messageId, idLen, out, outCap, outLen);
    }
    if (outLen) {
        *outLen = 0;
    }
    if (out && outCap > 0) {
        out[0] = '\0';
    }
    return RoboNotFound;
}

static const char *FindVar(const I18nMsgVar *vars,
                            size_t varCount,
                            const char *name,
                            size_t nameLen,
                            size_t *outVlen)
{
    size_t i;
    for (i = 0; i < varCount; i++) {
        size_t n = strlen(vars[i].name);
        if (n == nameLen && memcmp(vars[i].name, name, nameLen) == 0) {
            if (!vars[i].value) {
                *outVlen = 0;
                return "";
            }
            *outVlen = vars[i].valueLen ? vars[i].valueLen : strlen(vars[i].value);
            return vars[i].value;
        }
    }
    return NULL;
}

Result I18nMessageFormat(const char *templateUtf8,
                                       size_t templateLen,
                                       const I18nMsgVar *vars,
                                       size_t varCount,
                                       const I18nMsgOptions *options,
                                       char *out,
                                       size_t outCap,
                                       size_t *outLen)
{
    I18nMsgOptions scratch;
    const I18nMsgOptions *opt;
    size_t i = 0;
    size_t oi = 0;
    char tmp[2048];

    if (!templateUtf8 || !outLen) {
        return RoboInvalidArgument;
    }
    if (varCount > RoboI18nMsgVarsMax) {
        return RoboInvalidArgument;
    }
    opt = OptsOrDefault(options, &scratch);

    while (i < templateLen) {
        char c = templateUtf8[i];
        if (c == '{' && i + 1 < templateLen && templateUtf8[i + 1] == '{') {
            if (oi + 1 > sizeof(tmp)) {
                return RoboError;
            }
            tmp[oi++] = '{';
            i += 2;
            continue;
        }
        if (c == '}' && i + 1 < templateLen && templateUtf8[i + 1] == '}') {
            if (oi + 1 > sizeof(tmp)) {
                return RoboError;
            }
            tmp[oi++] = '}';
            i += 2;
            continue;
        }
        if (c == '{') {
            size_t j = i + 1;
            size_t vlen = 0;
            const char *val;
            while (j < templateLen && templateUtf8[j] != '}') {
                j++;
            }
            if (j >= templateLen) {
                return RoboParseError;
            }
            val = FindVar(vars, varCount, templateUtf8 + i + 1, j - (i + 1), &vlen);
            if (!val) {
                if (opt->varMissing == RoboI18nMsgVarMissLiteral) {
                    size_t chunk = j - i + 1u;
                    if (oi + chunk > sizeof(tmp)) {
                        return RoboError;
                    }
                    memcpy(tmp + oi, templateUtf8 + i, chunk);
                    oi += chunk;
                    i = j + 1;
                    continue;
                }
                return RoboNotFound;
            }
            if (oi + vlen > sizeof(tmp)) {
                return RoboError;
            }
            if (vlen) {
                memcpy(tmp + oi, val, vlen);
            }
            oi += vlen;
            i = j + 1;
            continue;
        }
        if (c == '}') {
            return RoboParseError;
        }
        if (oi + 1 > sizeof(tmp)) {
            return RoboError;
        }
        tmp[oi++] = c;
        i++;
    }
    return WriteOut(tmp, oi, out, outCap, outLen);
}

Result I18nMessageFormatId(const I18nCatalog *catalog,
                                          const char *messageId,
                                          size_t idLen,
                                          const char *context,
                                          size_t contextLen,
                                          const I18nLocale *locale,
                                          const I18nDecimal *pluralValue,
                                          const I18nMsgVar *vars,
                                          size_t varCount,
                                          const I18nMsgOptions *options,
                                          char *out,
                                          size_t outCap,
                                          size_t *outLen)
{
    char tmpl[1024];
    size_t n = 0;
    Result r;

    r = I18nMessageGet(catalog, messageId, idLen, context, contextLen, locale,
                              pluralValue, options, tmpl, sizeof(tmpl), &n);
    if (r != RoboOk) {
        return r;
    }
    return I18nMessageFormat(tmpl, n, vars, varCount, options, out, outCap,
                                    outLen);
}

uint32_t I18nCatalogEntryCount(const I18nCatalog *catalog)
{
    return catalog ? catalog->entryCount : 0u;
}

Result I18nCatalogEntryAt(const I18nCatalog *catalog,
                                         uint32_t index,
                                         const char **outLocale,
                                         const char **outId,
                                         const char **outContext,
                                         int *outIsPlural)
{
    if (!catalog || index >= catalog->entryCount) {
        return RoboInvalidArgument;
    }
    if (catalog->isDemo) {
        const MsgEntry *e = &MsgEntries[index];
        if (outLocale) {
            *outLocale = MsgString(e->locale);
        }
        if (outId) {
            *outId = MsgString(e->id);
        }
        if (outContext) {
            *outContext = (e->context >= 0) ? MsgString(e->context) : "";
        }
        if (outIsPlural) {
            *outIsPlural = (e->text < 0) ? 1 : 0;
        }
        return RoboOk;
    }
    {
        const uint8_t *base = catalog->blob + RoboMsgHeaderSize;
        const int32_t *raw = (const int32_t *)(base + index * RoboMsgEntrySize);
        if (outLocale) {
            *outLocale = BlobStr(catalog, raw[0]);
        }
        if (outId) {
            *outId = BlobStr(catalog, raw[1]);
        }
        if (outContext) {
            *outContext = (raw[2] >= 0) ? BlobStr(catalog, raw[2]) : "";
        }
        if (outIsPlural) {
            *outIsPlural = (raw[3] < 0) ? 1 : 0;
        }
    }
    return RoboOk;
}

Result I18nCatalogEntryText(const I18nCatalog *catalog,
                                           uint32_t index,
                                           int pluralSlot,
                                           char *out,
                                           size_t outCap,
                                           size_t *outLen)
{
    const char *t = NULL;

    if (!catalog || index >= catalog->entryCount || !outLen) {
        return RoboInvalidArgument;
    }
    if (catalog->isDemo) {
        const MsgEntry *e = &MsgEntries[index];
        if (pluralSlot < 0) {
            if (e->text < 0) {
                return RoboNotFound;
            }
            t = MsgString(e->text);
        } else if (pluralSlot <= (int)RoboI18nPluralOther) {
            if (e->plural[pluralSlot] < 0) {
                return RoboNotFound;
            }
            t = MsgString(e->plural[pluralSlot]);
        } else {
            return RoboInvalidArgument;
        }
    } else {
        const uint8_t *base = catalog->blob + RoboMsgHeaderSize;
        const int32_t *raw = (const int32_t *)(base + index * RoboMsgEntrySize);
        if (pluralSlot < 0) {
            if (raw[3] < 0) {
                return RoboNotFound;
            }
            t = BlobStr(catalog, raw[3]);
        } else if (pluralSlot <= (int)RoboI18nPluralOther) {
            if (raw[4 + pluralSlot] < 0) {
                return RoboNotFound;
            }
            t = BlobStr(catalog, raw[4 + pluralSlot]);
        } else {
            return RoboInvalidArgument;
        }
    }
    if (!t) {
        return RoboNotFound;
    }
    return WriteOut(t, strlen(t), out, outCap, outLen);
}
